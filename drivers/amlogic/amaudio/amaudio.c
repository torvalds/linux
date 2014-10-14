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

#include <mach/am_regs.h>
#include <linux/amlogic/amports/amaudio.h>

#include "amaudio.h"

#define AMAUDIO_DEVICE_COUNT    ARRAY_SIZE(amaudio_ports)

    
MODULE_DESCRIPTION("AMLOGIC Audio Control Interface driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin W");
MODULE_VERSION("1.0.0");

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define IRQ_OUT INT_I2S_DDR
#else
#define IRQ_OUT INT_AMRISC_DC_PCMLAST
#endif

typedef struct {
	unsigned int in_rd_ptr; /*i2s in read pointer */
	unsigned int in_wr_ptr; /*i2s in write pointer */
	unsigned int in_op_mode;
	unsigned int out_rd_ptr;/*i2s out read pointer */
	unsigned int out_wr_ptr; /*i2s out write pointer*/
	unsigned int out_op_mode;
	unsigned int type;
	unsigned int in_start;
	unsigned int in_size;
	unsigned int out_start;	
    unsigned int out_size;
    struct timer_list timer;

    unsigned level;
}amaudio_t;

typedef struct{
  const char *name;
  struct device* dev;
  const struct file_operations* fops;
}amaudio_port_t;
extern void audio_in_i2s_enable(int flag);

extern int if_audio_in_i2s_enable(void);
extern int if_audio_out_enable(void);
extern int if_958_audio_out_enable(void);
extern unsigned int read_i2s_rd_ptr(void);
extern unsigned int audio_in_i2s_wr_ptr(void);
extern unsigned int read_i2s_mute_swap_reg(void);
extern void audio_i2s_swap_left_right(unsigned int flag);
extern void audio_in_i2s_set_buf(u32 addr, u32 size);
extern void audio_in_i2s_enable(int flag);
extern int audio_out_buf_ready ;
extern int audio_in_buf_ready;

extern unsigned int aml_i2s_playback_start_addr;
extern unsigned int aml_i2s_capture_start_addr;
extern unsigned int aml_i2s_capture_buf_size;

static dev_t amaudio_devno;
static struct class* amaudio_clsp;
static struct cdev*  amaudio_cdevp;

static amaudio_t amaudio_in;
static amaudio_t amaudio_out;
static amaudio_t amaudio_inbuf;
static spinlock_t amaudio_lock;
static spinlock_t amaudio_clk_lock;

static char* amaudio_tmpbuf_in  = 0;
static char* amaudio_tmpbuf_out = 0;

static unsigned int music_wr_ptr = 0;
static unsigned int music_mix_flag = 1;
static unsigned int mic_mix_flag = 1;
static unsigned int enable_debug = 0;
static unsigned int enable_debug_dump = 0;
//--------------------------------------------
static unsigned int enable_resample_flag=0;
static unsigned int resample_type_flag=0; //0-->no resample  processing
                                           //1-->down resample processing
                                           //2-->up     resample processing
int resample_delta=0;
EXPORT_SYMBOL(resample_delta);
extern unsigned int timestamp_enable_resample_flag;
extern unsigned int timestamp_resample_type_flag;
//--------------------------------------------
#define DEBUG_DUMP 1

static unsigned short* dump_buf = 0;
static unsigned int dump_size = 512*1024;
static unsigned int dump_off = 0;


extern int aml_i2s_playback_enable;
extern unsigned int dac_mute_const;

static unsigned int audio_in_int_cnt = 0;
static unsigned int level2 = 0;
//static int last_out_status = 0;
//static int last_in_status = 0;
static int amaudio_in_started = 0;
static int amaudio_out_started = 0;

static int int_in_enable = 0;
static int int_out_enable = 0;

/* indicate if some error for i2s in */
unsigned char in_error_flag = 0;
EXPORT_SYMBOL(in_error_flag);
/* the error count for i2s in, 8 is a loop */
unsigned int in_error = 0;
EXPORT_SYMBOL(in_error);

/**********************************/
static int int_out = 0;
static int int_in = 0;

/**********************************/
static ssize_t put_audin_buf(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count);
static ssize_t put_audout_buf(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count);
static ssize_t put_audout_buf_direct(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count);
static ssize_t get_audin_buf(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count);
static ssize_t get_audout_buf(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count);

static int aprint(const char *fmt, ...)
{
  va_list args;
  int r=0;
  va_start(args, fmt);
  if(enable_debug){
    r = printk(fmt, args);
  }
  va_end(args);
  return r;
}

static int get_audin_ptr(void)
{
  int r = 0;
  if(audio_in_buf_ready){
	r = audio_in_i2s_wr_ptr() - READ_MPEG_REG(AUDIN_FIFO0_START);
  }else{
    r = -EINVAL;
  }
  return r;
}

static int get_audout_ptr(void)
{
  int r = 0;
  if(audio_out_buf_ready){
    r = read_i2s_rd_ptr() - READ_MPEG_REG(AIU_MEM_I2S_START_PTR);
  }else{
	r = -EINVAL;
  }
  return r;
}
/* direct audio start */

static int direct_audio_flag = DIRECT_AUDIO_OFF;
static int direct_left_gain = 128;
static int direct_right_gain = 128;

static void direct_audio_ctrl(int flag)
{
  if(flag == DIRECT_AUDIO_ON){
    direct_audio_flag = flag;
  }else if(flag == DIRECT_AUDIO_OFF){
    direct_audio_flag = flag;
  }
}

static void direct_audio_left_gain(int arg)
{
  if(arg < 0) arg = 0;
  if(arg > 256) arg = 256;

  direct_left_gain = arg;
}

static void direct_audio_right_gain(int arg)
{
  if(arg < 0) arg = 0;
  if(arg > 256) arg = 256;

  direct_right_gain = arg;
}
void audio_out_enabled(int flag)
{
  unsigned long irqflags;
  unsigned hwptr, tmp;
  if(amaudio_out_started == 0)
    return;

  local_irq_save(irqflags);
 
  if(flag){
    hwptr = get_audout_ptr();
    if(hwptr & 0x3f){
      printk("rd not aligned \n");
      hwptr &= ~0x3f;
    }
    amaudio_out.out_wr_ptr = (hwptr + 2880 + 1920) % amaudio_out.out_size;
    
    tmp = (hwptr / (30*64)) * 30;
    tmp = (tmp + 30 + (amaudio_out.out_size>>6)) % (amaudio_out.out_size>>6);
    WRITE_MPEG_REG_BITS(AIU_MEM_I2S_MASKS, tmp, 16, 16);

    amaudio_inbuf.level = 0;
    amaudio_inbuf.out_rd_ptr = amaudio_inbuf.out_wr_ptr;
    amaudio_inbuf.in_rd_ptr = amaudio_inbuf.out_rd_ptr;

    if(!int_out_enable){
      enable_irq(IRQ_OUT);
    }
    int_out_enable = 1;
    printk("audio out opened: hwptr=%d\n", hwptr);
  }else{    
    if(int_out_enable){
      disable_irq(IRQ_OUT);
    }
    int_out_enable = 0;
    printk("audio out closed\n");
  }  
  local_irq_restore(irqflags);
}
EXPORT_SYMBOL(audio_out_enabled);

void audio_in_enabled(int flag)
{
  unsigned hwptr, tmp;
  unsigned long irqflags;
  if(amaudio_in_started == 0)
    return;

  local_irq_save(irqflags);
  if(flag){
    hwptr = get_audin_ptr();
    tmp = (hwptr/3840) * 3840 + READ_MPEG_REG(AUDIN_FIFO0_START) + 3840;
    if(tmp > READ_MPEG_REG(AUDIN_FIFO0_END)-8){
      tmp -= amaudio_in.in_size;
    }
    amaudio_in.in_rd_ptr = (hwptr/3840) * 3840;
    WRITE_MPEG_REG(AUDIN_FIFO0_INTR, tmp);
    if(!int_in_enable){
      enable_irq(INT_AUDIO_IN);
    }
    int_in_enable = 1;
    printk("audio in opened: hwptr=%d\n", hwptr);
  }else{    
    if(int_in_enable){
      disable_irq(INT_AUDIO_IN);
    }
    int_in_enable = 0;
    in_error_flag = 0;
    in_error = 0;
    
    amaudio_in.in_rd_ptr = 0;
    
    amaudio_inbuf.level = 0;
    amaudio_inbuf.out_wr_ptr = 0;
    amaudio_inbuf.out_rd_ptr = 0;
    amaudio_inbuf.in_rd_ptr = 0;
    level2 = 0;
    printk("audio in closed\n");
  }
  local_irq_restore(irqflags);
}
EXPORT_SYMBOL(audio_in_enabled);


#if 0
static void amaudio_in_callback(unsigned long data)
#else
static irqreturn_t amaudio_in_callback(int irq, void*data)  
#endif
{
  
    amaudio_t* amaudio = (amaudio_t*)data;
    unsigned int hwptr = 0;
    unsigned int count = 0;
    int ret = 0;
    int tmp = 0, tmp1 = 0;

    audio_in_int_cnt ++;

    if(READ_MPEG_REG_BITS(AUDIN_FIFO_INT, 0, 1) == 1){
     aprint("audin overflow\n");
    }

    spin_lock(&amaudio_clk_lock);
  
  int_in = 1;
  if(int_out){
    aprint("audio out INT breaked\n");
  }
    do{
#if 0
      if(direct_audio_flag == DIRECT_AUDIO_OFF){
          amaudio->in_rd_ptr = -1;
      }
#endif
     /* get the raw data  and put to temp buffer */
      hwptr = get_audin_ptr(); /* get audin hw ptr */
      hwptr = (hwptr - 64 + amaudio->in_size) % (amaudio->in_size); /* keep 64 bytes latency */
      hwptr &= (~0x3f);
      count = (hwptr - amaudio->in_rd_ptr + amaudio->in_size) % amaudio->in_size;
      count &= (~0x3f);/* 64 bytes align */
      /* if no data or the input hw not work*/
      if(count == 0){
          aprint("no data to read\n");
          break;
      }
      /* if 30ms buffer ready*/
      if(count > 3840*3){
#if 0        
        amaudio->in_rd_ptr = (hwptr - 3840 + amaudio->in_size) % amaudio->in_size;
        if(amaudio->in_rd_ptr & 0x3f){
          aprint("in rd not align\n");
        }
        count = 3840;
#else
        if(amaudio->in_rd_ptr + 3840*2 < amaudio->in_size){
          ret = get_audin_buf(amaudio, (void*)amaudio_tmpbuf_in, 
              (void*)(amaudio->in_start+amaudio->in_rd_ptr),3840*2);
          if(ret !=0){
            aprint("skip 3840*2 error\n");
          }
        }else{
          tmp = amaudio->in_size - amaudio->in_rd_ptr;
          if(tmp & 0x3f){
            tmp = (tmp + 64) & (~0x3f);
          }
          ret = get_audin_buf(amaudio,(void*)amaudio_tmpbuf_in,
              (void*)(amaudio->in_start+amaudio->in_rd_ptr), tmp);
          if(ret !=0){
            aprint("skip %d error\n", tmp);
          }

          ret = get_audin_buf(amaudio, (void*)amaudio_tmpbuf_in,
              (void*)(amaudio->in_start+amaudio->in_rd_ptr), 3840*2-tmp);
          if(ret != 0){
            aprint("skip %d error\n", 3840*2-tmp);
          }

        }
        count -= 3840*2;
#endif        
      }
      if(count > 3840){
        aprint("read count : %d, hwptr=%d, rd=%d\n", count, hwptr, amaudio->in_rd_ptr);
      }
      if(count/2 + amaudio_inbuf.level > amaudio_inbuf.in_size){
          aprint("no space to store count=%d, level=%d, size=%d\n", count/2, amaudio_inbuf.level, amaudio_inbuf.in_size);
          amaudio_inbuf.out_rd_ptr += count/2;
          amaudio_inbuf.out_rd_ptr %= amaudio_inbuf.in_size;
          amaudio_inbuf.level -= count/2;
      }
      /* check if user buffer over writen by us*/
      if((amaudio_inbuf.out_wr_ptr + count/2 > amaudio_inbuf.in_rd_ptr && 
          amaudio_inbuf.out_wr_ptr < amaudio_inbuf.in_rd_ptr) || 
          (amaudio_inbuf.out_wr_ptr > amaudio_inbuf.in_rd_ptr && 
           amaudio_inbuf.out_wr_ptr+count/2 > amaudio_inbuf.in_rd_ptr + amaudio_inbuf.in_size )){
        aprint("music buf conflict, hwptr %d, count %d, music %d\n", 
            amaudio_inbuf.out_wr_ptr, count/2, amaudio_inbuf.in_rd_ptr);
      }
      /* check if the temp buf ready */
      if(amaudio_tmpbuf_in == 0){
        aprint("fatal error !!!\n");
      }

//aprint("hwptr=%d, rd=%d, count=%d\n", hwptr, amaudio->in_rd_ptr, count);      
      /* copy the data to temp buf first */
      if(amaudio->in_rd_ptr + count < amaudio->in_size){
        ret = get_audin_buf(amaudio, (void*)amaudio_tmpbuf_in, (void*)(amaudio->in_start + amaudio->in_rd_ptr), count);
        if(ret != 0){
          aprint("read audio in error 1: %d, return %d\n", count, ret);
          break;
        }
      }else{
        tmp = amaudio->in_size - amaudio->in_rd_ptr;
        if(tmp & 0x3f){
          tmp = (tmp + 64) & (~0x3f);
        }
        ret = get_audin_buf(amaudio, (void*)amaudio_tmpbuf_in, (void*)(amaudio->in_start + amaudio->in_rd_ptr), tmp);
        if(ret != 0){
          aprint("read audio in error 2: %d, return %d, rd %d\n", tmp, ret, amaudio->in_rd_ptr);
          break;
        }
        
        tmp1 = count - tmp;
        if(tmp1 < 0){
          aprint("tmp1=%d\n", tmp1);
        }else if(tmp1 != 0){
          if(tmp1 & 0x3f){
            aprint("tmp1 not align: %d\n", tmp1);
          }
          ret = get_audin_buf(amaudio, (void*)(amaudio_tmpbuf_in + tmp/2), (void*)(amaudio->in_start + amaudio->in_rd_ptr), tmp1);
          if(ret != 0){
            aprint("read audio in error 3: %d, return %d\n", tmp1, ret);
            break;
          }
        }
      }
      /* send the data to middle buf*/
      if(amaudio_inbuf.out_wr_ptr + count/2 < amaudio_inbuf.out_size){
        memcpy((void*)(amaudio_inbuf.out_start + amaudio_inbuf.out_wr_ptr), amaudio_tmpbuf_in, count/2);
      }else{
        tmp = amaudio_inbuf.out_size - amaudio_inbuf.out_wr_ptr;
        memcpy((void*)(amaudio_inbuf.out_start + amaudio_inbuf.out_wr_ptr), amaudio_tmpbuf_in, tmp);
        tmp1 = count/2 - tmp;
        memcpy((void*)(amaudio_inbuf.out_start), amaudio_tmpbuf_in + tmp, tmp1);
      }
      
      amaudio_inbuf.out_wr_ptr += count/2;
      amaudio_inbuf.out_wr_ptr %= amaudio_inbuf.out_size;
      if(enable_debug_dump && dump_buf){
        int ii=0;        
        for(ii=0; ii< count/4; ii++){
          dump_buf[dump_off++] =((unsigned short*)amaudio_tmpbuf_in)[ii];
          if(dump_off == dump_size){
            enable_debug_dump = 0;
            dump_off = 0;
            printk("dump finished\n");
          }
        }
      }
    }while(0);

    /***************************************/
    

    /***************************************/
#if 0    
    spin_unlock(&amaudio_clk_lock);
    mod_timer(&amaudio->timer, jiffies + 1);
#else
    tmp = READ_MPEG_REG(AUDIN_FIFO0_INTR);
    tmp1 = READ_MPEG_REG(AUDIN_FIFO0_END) - 8;
    tmp = tmp + 3840;
    if(tmp >= tmp1){
      tmp -= amaudio->in_size;
    }

   
    WRITE_MPEG_REG(AUDIN_FIFO0_INTR, tmp);
    WRITE_MPEG_REG(AUDIN_FIFO_INT, 0);
    {
      int tmp2 = audio_in_i2s_wr_ptr();
      /* the next int should come 10-15ms after*/
      if(!(tmp2 < tmp && tmp2 + 5760 >= tmp) && !(tmp2 > tmp && tmp2 + 5760 - amaudio->in_size >= tmp)){
        aprint("this is a invalid interrupt: INTR 0x%x, ptr 0x%x\n", tmp, tmp2);
      }
    }
    amaudio_inbuf.level += count/2;
    if(amaudio_inbuf.level > amaudio_inbuf.out_size)
      amaudio_inbuf.level = amaudio_inbuf.out_size;

    level2 += count/2;
    if(level2 > amaudio_inbuf.out_size)
      level2 = amaudio_inbuf.out_size;

 //   aprint("+ level = %d, count=%d\n", amaudio_inbuf.level, count/2);
    
//err: 
 int_in = 0;    

    spin_unlock(&amaudio_clk_lock);
    return IRQ_HANDLED;
#endif    
}
#if 0
static void amaudio_out_callback(unsigned long data)
#else
static irqreturn_t amaudio_out_callback(int irq, void* data)  
#endif  
{
  
    amaudio_t* amaudio = (amaudio_t*)data;
    unsigned int hwptr = 0;
    unsigned int count = 0;
    unsigned int scount = 0;
    unsigned int tmp;
    int ret = 0;

    spin_lock(&amaudio_clk_lock);

    int_out = 1;
    if(int_in){
      aprint("audio in INT breaked\n");
    }  
  do{ 
      hwptr = get_audout_ptr();
      if(direct_audio_flag == DIRECT_AUDIO_OFF){
       // amaudio->out_wr_ptr = -1;
//        break;
      }
      hwptr = (hwptr + 2880) % amaudio->out_size;
      if(0){//amaudio->out_wr_ptr == -1){
        amaudio->out_wr_ptr = (hwptr + 1920) % amaudio->out_size;
        amaudio_inbuf.out_rd_ptr = amaudio_inbuf.out_wr_ptr;
        amaudio_inbuf.out_rd_ptr &= ~0x3f;
        amaudio_inbuf.level = 0;

        level2 = 0;
        

        /* 10 msec later, the next interrupt comming */
        tmp = READ_MPEG_REG_BITS(AIU_MEM_I2S_MASKS, 16, 16);
        tmp = (tmp + 30 + (amaudio->out_size>>6)) % (amaudio->out_size>>6);
        WRITE_MPEG_REG_BITS(AIU_MEM_I2S_MASKS, tmp, 16, 16);
        aprint("The first time INT for audio out\n");
        goto err;
      }

      /* how much space */
      count = (amaudio->out_wr_ptr -hwptr + amaudio->out_size) % amaudio->out_size;
      count &= (~0x3f);
      if(count > 5760 && count < (amaudio->out_size-2880+64)){ /* 30ms delay*/
        tmp = amaudio->out_wr_ptr;
        amaudio->out_wr_ptr = (hwptr+64)&(~0x3f);
        aprint("wr set from %d to %d, hwptr=%d, count=%d\n", tmp, amaudio->out_wr_ptr, hwptr, count);
      }

 //     aprint("out: count %8d, wr %8d, hw %8d\n", count, amaudio->out_wr_ptr, hwptr);
      /* how many datas */
      scount = amaudio_inbuf.level & (~0x3f);
     
      if(amaudio_inbuf.level <= 0){  /* no fresh data to read */
        aprint("no fresh data to read: %d, hwptr=%x, INTR=%x, AUDIN=%x, INTCNT %d, INT:%d\n", \
            amaudio_inbuf.level, get_audout_ptr(), READ_MPEG_REG(AUDIN_FIFO0_INTR), get_audin_ptr(), audio_in_int_cnt, READ_MPEG_REG(AUDIN_FIFO_INT));
        /* check if audio in error*/
        if(if_audio_in_i2s_enable() == 0){
          aprint("audio in closed++\n");
        }
        count = 0;
        break;
      }

      if(scount > 2*2880){ /* too slow to write? */
        aprint("amaudio_inbuf.level=%d, wr=%d,rd=%d\n", amaudio_inbuf.level, amaudio_inbuf.out_wr_ptr, amaudio_inbuf.out_rd_ptr);
        tmp = amaudio_inbuf.out_rd_ptr;
        amaudio_inbuf.out_rd_ptr = (amaudio_inbuf.out_wr_ptr - 3840 + amaudio_inbuf.out_size) % amaudio_inbuf.out_size;
        amaudio_inbuf.out_rd_ptr &= ~0x3f;
        amaudio_inbuf.level = (amaudio_inbuf.out_wr_ptr - amaudio_inbuf.out_rd_ptr + amaudio_inbuf.out_size) % amaudio_inbuf.out_size;
        
        scount = (amaudio_inbuf.out_wr_ptr - amaudio_inbuf.out_rd_ptr + amaudio_inbuf.out_size) % amaudio_inbuf.out_size;
        scount &= ~0x3f;
        
        aprint("rd set from %d to %d, count=%d, wr=%d, rd=%d\n", tmp, amaudio_inbuf.out_rd_ptr, scount, amaudio_inbuf.out_wr_ptr, amaudio_inbuf.out_rd_ptr);
//        break;
      }
      
      count = scount;
      /* copy the data to temp buf */
      if(amaudio_inbuf.out_rd_ptr + count <= amaudio_inbuf.out_size){
        memcpy((void*)amaudio_tmpbuf_out, (void*)(amaudio_inbuf.out_start + amaudio_inbuf.out_rd_ptr), count);
      }else{
        tmp = amaudio_inbuf.out_size - amaudio_inbuf.out_rd_ptr;
        memcpy((void*)amaudio_tmpbuf_out, (void*)(amaudio_inbuf.out_start + amaudio_inbuf.out_rd_ptr), tmp);
        memcpy((void*)(amaudio_tmpbuf_out + tmp), (void*)amaudio_inbuf.out_start, count - tmp);
      }
      /* update the middle buf pointer */
      amaudio_inbuf.out_rd_ptr += count;
      amaudio_inbuf.out_rd_ptr %= amaudio_inbuf.out_size;
      /* send the data to hw buf */
      if(amaudio->out_wr_ptr + count <= amaudio->out_size){
        ret = put_audout_buf_direct(amaudio, (void*)(amaudio->out_start + amaudio->out_wr_ptr), \
            amaudio_tmpbuf_out, count);
        if(ret != 0){
          aprint("write error 1: write %d, return %d \n", count, ret);
          break;
        }
      }else{
        tmp = amaudio->out_size - amaudio->out_wr_ptr;
        ret = put_audout_buf_direct(amaudio, (void*)(amaudio->out_start + amaudio->out_wr_ptr), \
            amaudio_tmpbuf_out, tmp);
        if(ret != 0){
          aprint("write error 2: write %d, return %d\n", tmp, ret);
          break;
        }
        ret = put_audout_buf_direct(amaudio, (void*)(amaudio->out_start + amaudio->out_wr_ptr), \
            amaudio_tmpbuf_out + tmp, count -tmp);
        if(ret != 0){
          aprint("write error 3: write %d, return %d\n", count-tmp, ret);
          break;
        }
      }
    }while(0);
#if 0
    spin_unlock(&amaudio_clk_lock);
    mod_timer(&amaudio->timer, jiffies + 1);
#else
    tmp = READ_MPEG_REG_BITS(AIU_MEM_I2S_MASKS, 16, 16);
    tmp = (tmp + 30 + (amaudio->out_size>>6)) % (amaudio->out_size>>6);
    WRITE_MPEG_REG_BITS(AIU_MEM_I2S_MASKS, tmp, 16, 16);
   
    if(amaudio_inbuf.level < count){
      aprint("buf error : level = %d, count = %d\n", amaudio_inbuf.level, count);
    }
    amaudio_inbuf.level -= count;

 //   aprint("- level = %d\n", amaudio_inbuf.level);

err:
    int_out = 0;    

    spin_unlock(&amaudio_clk_lock);
    return IRQ_HANDLED;
#endif
}

/* direct audio end */ 
static ssize_t amaudio_write(struct file *file, const char *buf,
                                size_t count, loff_t * ppos);

static ssize_t amaudio_read(struct file *file, char __user *buf, 
															size_t count, loff_t * ppos);
static int amaudio_open(struct inode *inode, struct file *file);

static int amaudio_release(struct inode *inode, struct file *file);

static long amaudio_ioctl(struct file *file,
                        unsigned int cmd, unsigned long arg);

static int amaudio_utils_open(struct inode *inode, struct file *file);

static int amaudio_utils_release(struct inode *inode, struct file *file);

static long amaudio_utils_ioctl(struct file *file,
                        unsigned int cmd, unsigned long arg);
const static struct file_operations amaudio_out_fops = {
  .owner    =   THIS_MODULE,
  .open     =   amaudio_open,
  .release  =   amaudio_release,
  .write    =   amaudio_write,
  .read     =   amaudio_read,
  .unlocked_ioctl    =   amaudio_ioctl,
};

const static struct file_operations amaudio_in_fops = {
  .owner    =   THIS_MODULE,
  .open     =   amaudio_open,
  .release  =   amaudio_release,
  .write    =   amaudio_write,
  .read     =   amaudio_read,
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
  .open     =   amaudio_utils_open,
  .release  =   amaudio_utils_release,
  .unlocked_ioctl    =   amaudio_utils_ioctl,
};

static amaudio_port_t amaudio_ports[]={
  {
    .name = "amaudio_out",
    .fops = &amaudio_out_fops,
  },
  {
    .name = "amaudio_in",
    .fops = &amaudio_in_fops,
  },
  {
    .name = "amaudio_ctl",
    .fops = &amaudio_ctl_fops,
  },
  {
    .name = "amaudio_utils",
    .fops = &amaudio_utils_fops,
  },
};

static ssize_t put_audin_buf(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count)
{
    int i, j;
    unsigned int *left, *right;
    unsigned short *src;

    size_t tmp_count = count;
    spin_lock_irq(&amaudio_lock);
    if(amaudio->in_wr_ptr + count > amaudio->in_size){
      count = amaudio->in_size - amaudio->in_wr_ptr;
//      aprint("i2s in buffer write block too big: %x\n", tmp_count);
    }
    
    count >>= 6;
    count <<= 6;
    if(count < 64){
      spin_unlock_irq(&amaudio_lock);
      return tmp_count;
    }
    left = (unsigned int *) dbuf;
    right = left + 8;
    src = (unsigned short*) sbuf;

    for(i = 0; i < count; i+= 64){
      for(j = 0; j < 8; j ++){
        *left ++ = (*src ++) << 8;
        *right ++ = (*src ++) << 8;
      }
      left += 8;
      right += 8;
    }
    amaudio->in_wr_ptr = (amaudio->in_wr_ptr + count) % amaudio->in_size;
    spin_unlock_irq(&amaudio_lock);
    return tmp_count - count;
}
static ssize_t put_audout_buf_direct(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count)
{
    int i, j;
    signed short *left, *right;
    signed short * src;
    
    signed int samp = 0, sampL, sampR, sampLR;
    unsigned int tmp_count= count;
    
//    spin_lock(&amaudio_lock);
    if(amaudio->out_wr_ptr + count > amaudio->out_size){
      count = amaudio->out_size - amaudio->out_wr_ptr;
      aprint("i2s outbuf write block too big: %d, -> count %d, wr %d, size %d\n", tmp_count, count, amaudio->out_wr_ptr, amaudio->out_size);
    }

    count >>= 6;
    count <<= 6;
    if(count < 64){
//      spin_unlock(&amaudio_lock);
      return tmp_count;
    }
    
    if(0)
    {
     unsigned ppp = get_audout_ptr();
     aprint("+hwptr=%d,wr=%d,count=%d\n",ppp,amaudio->out_wr_ptr, count);
    }

    left = (signed short*) dbuf;
    right = left + 16;
    src = (signed short*)sbuf;
    for(i = 0; i < count; i += 64){
      for(j = 0; j < 16; j ++){
        if(mic_mix_flag == 1){    
          sampL = *src ++;
          sampR = *src ++;
          sampLR = ((sampL * direct_left_gain) >> 8) + ((sampR * direct_right_gain) >> 8);
          samp = *left + sampLR;
          if(samp > 0x7fff) samp = 0x7fff;
          if(samp < -0x8000) samp = -0x8000;
          *left ++ = samp&0xffff;

          samp = *right + sampLR;

          if(samp > 0x7fff) samp = 0x7fff;
          if(samp < -0x8000) samp = -0x8000;
          *right ++ = samp&0xffff;
        }else{   
        //  *left ++ = *src ++;
        //  *right ++ = *src ++;
        }
      }
      left += 16;
      right += 16;
    }
    amaudio->out_wr_ptr = (amaudio->out_wr_ptr + count) % amaudio->out_size;
//    spin_unlock(&amaudio_lock);
    return tmp_count - count;
}

static ssize_t put_audout_buf(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count)
{
    int i, j;
    signed short *left, *right;
    signed short * src;
    
    signed int samp = 0;
    unsigned int tmp_count= count;
    
    spin_lock_irq(&amaudio_lock);

    if(amaudio->out_wr_ptr + count > amaudio->out_size){
      count = amaudio->out_size - amaudio->out_wr_ptr;
//      aprint("i2s outbuf write block too big: %x\n", tmp_count);
    }

    count >>= 6;
    count <<= 6;
    if(count < 64){
      spin_unlock_irq(&amaudio_lock);
      return tmp_count;
    }
    if(1)
    {
     unsigned hwptr = get_audout_ptr();
     if(hwptr >amaudio->out_wr_ptr && hwptr < amaudio->out_wr_ptr+count){
       aprint("audio out buffer conflict\n");
     }
    }
    if(music_mix_flag){
      left = (signed short*) dbuf;
      right = left + 16;
      src = (signed short*)sbuf;
      for(i = 0; i < count; i += 64){
        for(j = 0; j < 16; j ++){
          samp = *left;
          samp += *src++;
          if(samp > 0x7fff) samp = 0x7fff;
          if(samp < -0x8000) samp = -0x8000;
          *left ++ = samp&0xffff;

          samp = *right;
          samp += *src ++;
          if(samp > 0x7fff) samp = 0x7fff;
          if(samp < -0x8000) samp = -0x8000;
          *right ++ = samp&0xffff;
        }
        left += 16;
        right += 16;
      }
    }
    amaudio->out_wr_ptr = (amaudio->out_wr_ptr + count) % amaudio->out_size;

    music_wr_ptr = amaudio->out_wr_ptr;

    spin_unlock_irq(&amaudio_lock);
    return tmp_count - count;
}


static ssize_t amaudio_write(struct file *file, const char *buf,
	size_t count, loff_t * ppos)
{
  amaudio_t * amaudio = (amaudio_t *)file->private_data;
  int len = 0;
  char* tmpBuf;

  if(count <=0 )
	return -EINVAL;
  tmpBuf = (char*)kmalloc(count, GFP_KERNEL);
  if(tmpBuf == 0){
    aprint("amaudio_write alloc failed\n");
    return -ENOMEM;
  }

  if(amaudio->type == 1){
	if(audio_in_buf_ready == 0){
	  aprint("amaudio input can not write now\n");
      kfree(tmpBuf);
	  return -EINVAL;
	}
	if(amaudio->in_op_mode == 0){ /* 32 bit block mode */
	  len = copy_from_user((void*)(amaudio->in_wr_ptr+amaudio->in_start), (void*)buf, count);
	}else if(amaudio->in_op_mode == 1){ /* 16bit interleave mode */
      if(copy_from_user((void*)tmpBuf, (void*)buf, count) != 0){
        aprint("amaudio in: copy from user failed\n");
      }
	  len = put_audin_buf(amaudio, (void*)(amaudio->in_wr_ptr + amaudio->in_start), (void*)buf, count * 2);
	}
  }else if(amaudio->type == 0){
	if(audio_out_buf_ready == 0){
	  aprint("amaudio output can not write now\n");
      kfree(tmpBuf);
	  return -EINVAL;
	}
	if(amaudio->out_op_mode == 0){ /* 32 bit block mode */
	  len = copy_from_user((void*)(amaudio->out_wr_ptr+amaudio->out_start), (void*)buf, count);
	}else if(amaudio->out_op_mode == 1){/* 16bit interleave mode */
      if(copy_from_user((void*)tmpBuf, (void*)buf, count) != 0){
        aprint("amaudio out: copy from user failed\n");
      }
	  len = put_audout_buf(amaudio, (void*)(amaudio->out_wr_ptr + amaudio->out_start), (void*)buf, count);
	}
  }
  kfree(tmpBuf);
  return count - len;
}
/**
 * extract 16bit samples from HW buf
 * parameters:
 *  dbuf - dest buf
 *  sbuf - source buf
 *  count - how many bytes to extract
 */
static ssize_t get_audin_buf(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count)
{
    int i,j;
    unsigned short *out;
    unsigned int *left, *right;
#if 0    
    left    = (unsigned int*)sbuf;
    right   = left + 8;
    out = (unsigned short*)dbuf;

    for(i = 0; i< count; i+=64){      
      for(j=0; j<8; j++){
        *out++ = ((*left++)>>8)&0xffff;
        *out++ = ((*right++)>>8)&0xffff;
      }
      left += 8;
      right += 8;
    }
    return 0;
#else
    unsigned int *magic;
    unsigned int *start;
    unsigned int temp[16];
    ssize_t res = 0;
    size_t tmp_count = count;
    unsigned int in_error_last;
    
 //   spin_lock(&amaudio_lock);

    if(amaudio->in_rd_ptr + count > amaudio->in_size){
      count = amaudio->in_size - amaudio->in_rd_ptr;
      if((count%64) && in_error_flag){
 //         aprint("in_rd_ptr = %x, count= %x\n", amaudio->in_rd_ptr, tmp_count);
          count = ((count + 64) >> 6) << 6;
          if(count > tmp_count){
            aprint("please read a 32 bytes block\n");
   //         spin_unlock(&amaudio_lock);
            return tmp_count;
          }
      }
//      aprint("the i2s in buffer read block too big%x\n", tmp_count);
    }

	count >>= 6;
	count <<= 6;
	/* too few data request */
	if(count < 64){
//      spin_unlock(&amaudio_lock);
	  return tmp_count;
	}
	left = (unsigned int*)sbuf;
	right = left + 8;
	out = (unsigned short*)dbuf;
	for(i=0; i<count-64; i+= 64){
	  for(j=0; j<8; j++){
		*out++ = ((*left++)>>8)&0xffff;
		*out++ = ((*right++)>>8)&0xffff;
	  }
	  left += 8;
	  right += 8;
	}
	if(amaudio->in_rd_ptr + count >= amaudio->in_size){
	  /* extract the last 8 bytes */
	  magic = (unsigned int*)(amaudio->in_start + amaudio->in_size - 8);
	  start = (unsigned int*)(amaudio->in_start);
	  //     aprint("in_rd_ptr=%x, count= %x\n", amaudio->in_rd_ptr, count);
	  /* if the datas not valid this time */
	  in_error_last = in_error;
	  if(magic[0] == 0x78787878 && magic[1] == 0x78787878){
		in_error ++;
		in_error_flag = 1;
		aprint("audin in error: %d\n", in_error);
		in_error &= 7;
	  }
	  //aprint("this is end: %x\n", in_error);
	  if(in_error_flag && in_error){
		right -= 8;
//		aprint("start=%x, size=%x, right = %x\n", amaudio->in_start, amaudio->in_size, right);
		for(i=0; i< 16 - 2*in_error; i++){
		  temp[i] = *right++;
		}
//		aprint("=============\n");

		for(i=0; i< 2*in_error; i++){
		  temp[i+16-2*in_error] = start[i];
		}
        /* calc the next operate pointer */
        if(in_error_last != in_error){
          amaudio->in_rd_ptr = (amaudio->in_rd_ptr + count + 8) % amaudio->in_size;
        }else{
          amaudio->in_rd_ptr = (amaudio->in_rd_ptr + count) % amaudio->in_size;
        }

        res = 0;
        for(i=0; i<8; i++){
          *out++ = (temp[i+0]>>8)&0xffff;
          *out++ = (temp[i+8]>>8)&0xffff;
        }
	    magic[0] = magic[1] = 0x78787878;
      }else if(in_error_flag){
        /* reset the pointer, 64 byte read next time */
        for(i=0; i<8; i++){
          *out++ = (start[i+0]>>8)&0xffff;
          *out++ = (start[i+8]>>8)&0xffff;
		}
		res = 0; 
		amaudio->in_rd_ptr = 64;	
		magic[0] = magic[1] = 0x78787878;
	  }else{
		for(i=0; i<8; i++){
		  *out++ = ((*left++)>>8)&0xffff;
		  *out++ = ((*right++)>>8)&0xffff;
		}
		res = 0;
		amaudio->in_rd_ptr = 0;
		magic[0] = magic[1] = 0x78787878;
	  }

	}else{
	  /* normal case */
	  for(i=0; i<8; i++){
		*out++ = ((*left++)>>8)&0xffff;
		*out++ = ((*right++)>>8)&0xffff;
	  }
	  amaudio->in_rd_ptr = (amaudio->in_rd_ptr + count) % amaudio->in_size;
	}
   // aprint("tmp_count=%x, count=%x, res=%x\n", tmp_count, count, res);
//    spin_unlock(&amaudio_lock);
    return tmp_count - count + res;
#endif
}

static ssize_t get_audin_buf16(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count)
{
    int i;
    size_t tmp_count = count;
    unsigned short * src, *out;
    spin_lock_irq(&amaudio_lock);
    if(amaudio->in_rd_ptr + count > amaudio->in_size){
      count = amaudio->in_size - amaudio->in_rd_ptr;
    }
    count &= ~0x3; /* the buffer is 16bit interleave samples*/
    if(count < 4){
      spin_unlock_irq(&amaudio_lock);
      return count;
    }
    if(1)
    {
      unsigned hwptr = amaudio->out_wr_ptr;
      if((hwptr > amaudio->in_rd_ptr && hwptr < amaudio->in_rd_ptr+count)){
        aprint("audio in buffer conflict\n");
      }
    }
    src = (unsigned short*)sbuf;
    out = (unsigned short*)dbuf;
    for(i = 0; i< count ; i+=2){
      *out ++ = *src ++;
    }
    amaudio->in_rd_ptr = (amaudio->in_rd_ptr + count )%amaudio->in_size;
    spin_unlock_irq(&amaudio_lock);
    return tmp_count - count;
}

static ssize_t get_audout_buf(amaudio_t* amaudio, void* dbuf, void* sbuf, size_t count)
{
    int i, j;
    unsigned short *left, *right;
    unsigned short *out;
    size_t tmp_count = count;
    
    spin_lock_irq(&amaudio_lock);

    if(amaudio->out_rd_ptr + count > amaudio->out_size){
      count = amaudio->out_size - amaudio->out_rd_ptr;
//      aprint("the count too big: %x\n", tmp_count);
    }
    count >>= 6;
    count <<= 6;
    
    if(count < 64){
      spin_unlock_irq(&amaudio_lock);
      return tmp_count;
    }
    
    if(1){
      unsigned hwptr = get_audout_ptr();
      if(amaudio->out_rd_ptr < hwptr && amaudio->out_rd_ptr + count > hwptr){
        aprint("audio out read conflict\n");
      }
    }

    left = (unsigned short*)sbuf;
    right = left + 16;
    out = (unsigned short*) dbuf;
    for(i = 0; i< count; i += 64){
      for(j=0; j<16; j++){
        *out ++ = *left ++;
        *out ++ = *right ++;
      }
      left += 16;
      right += 16;
    }
    amaudio->out_rd_ptr = (amaudio->out_rd_ptr + count) % amaudio->out_size;
    
    if(level2 < count){
      aprint("audio buffer error: level2 = %d, count= %d\n", level2, count);
    }
    level2 -= count;

    spin_unlock_irq(&amaudio_lock);

    return  tmp_count - count;
}

static ssize_t amaudio_read(struct file *file, char __user *buf, 
															size_t count, loff_t * ppos)
{
	amaudio_t * amaudio = (amaudio_t *)file->private_data;
	int len = 0;
    char* tmpBuf;
    if(count <= 0) {
      aprint("amaudio can not read less than 0\n");
      return -EINVAL;
    }
    tmpBuf = (char*)kmalloc(count, GFP_KERNEL);
    if(tmpBuf == 0){
      aprint("amaudio_read alloc memory failed\n");
      return -ENOMEM;
    }
	if(amaudio->type == 1){
		if(audio_in_buf_ready == 0){
			aprint("amaudio input can not read now\n");
            kfree(tmpBuf);
			return -EINVAL;
		}
        
        if(if_audio_in_i2s_enable() == 0){
          aprint("amaudio read: audio in be closed!!!\n");
        }

        if(amaudio->in_op_mode == 0){   /* 32bit block mode */
          len = copy_to_user((void*)buf, (void*)(amaudio->in_rd_ptr+amaudio->in_start), count);
          /* the appilcation request "count" bytes */
          memset((void*)(amaudio->in_rd_ptr+amaudio->in_start), 0x78, count); /*the HW buf is 32bit*/
        }else if(amaudio->in_op_mode == 1){      /* 16 bit interleave mode */  
#if 0          
          tmp = amaudio->in_rd_ptr;
          len = get_audin_buf(amaudio, (void*)buf, (void*)(amaudio->in_rd_ptr+amaudio->in_start), count * 2);
         // memset((void*)(tmp + amaudio->in_start), 0x78, count * 2);  /* the HW buf is 32bit */
          len /= 2;
#else
          len = get_audin_buf16(&amaudio_inbuf, (void*)tmpBuf, (void*)(amaudio_inbuf.in_rd_ptr + amaudio_inbuf.in_start), count);
          if(copy_to_user((void*)buf, (void*)tmpBuf, count) != 0){
            aprint("amaudio in: should not be here, please check if read out the whole size\n");
          }
#endif          
        }
	}
	else if(amaudio->type == 0){
		if(audio_out_buf_ready == 0){
			aprint("amaudio output can not read now\n");
            kfree(tmpBuf);
			return -EINVAL;
		}

        if(if_audio_out_enable() == 0){
          aprint("amaudio read: audio out be closed !!!\n");
        }

        if(amaudio->out_op_mode == 0){  /* 32 bit block mode */
		  len = copy_to_user((void*)buf, (void*)(amaudio->out_rd_ptr+amaudio->out_start), count);
          /* the appilcation request "count" bytes */
        }else if(amaudio->out_op_mode == 1){/* 16bit, two samples, also 32bit*/
          len = get_audout_buf(amaudio, (void*)tmpBuf, (void*)(amaudio->out_rd_ptr + amaudio->out_start), count);
          if(copy_to_user((void*)buf, (void*)tmpBuf, count-len) != 0){
            aprint("amaudio out: should not be here, please check if read out the whole size\n");
          }
        }
	}
    kfree(tmpBuf);
	return count - len;
}

static int audout_irq_alloced = 0;

static int amaudio_open(struct inode *inode, struct file *file)
{
  amaudio_port_t* this = &amaudio_ports[iminor(inode)];      
  amaudio_t * amaudio = kzalloc(sizeof(amaudio_t), GFP_KERNEL);
  int tmp=0;
  if (audio_in_buf_ready && iminor(inode)== 1){
    amaudio->in_size  = READ_MPEG_REG(AUDIN_FIFO0_END) - READ_MPEG_REG(AUDIN_FIFO0_START) + 8;
    amaudio->in_start = aml_i2s_capture_start_addr;
    amaudio->in_rd_ptr = 0;
    amaudio->in_wr_ptr = 0;
    memcpy(&amaudio_in, amaudio, sizeof(amaudio_t));
  }
  
  if (audio_out_buf_ready && iminor(inode) == 0){
    amaudio->out_size = READ_MPEG_REG(AIU_MEM_I2S_END_PTR) - READ_MPEG_REG(AIU_MEM_I2S_START_PTR) + 64;
    amaudio->out_start = aml_i2s_playback_start_addr;
    amaudio->out_wr_ptr = 0;
    amaudio->out_rd_ptr = 0;
    
    memcpy(&amaudio_out, amaudio, sizeof(amaudio_t));
  }
  if(iminor(inode) == 0){ // audio out
    printk("open audio out: start=%x\n", amaudio->out_start);
	amaudio->type = 0;
    if(audio_out_buf_ready == 0){
      printk("ALSA playback not ready, please try again!!!\n");
      goto error;
    }
#if 0
    amaudio_out.timer.function = &amaudio_out_callback;
    amaudio_out.timer.data = (unsigned long)(&amaudio_out);
    init_timer(&amaudio_out.timer);
    mod_timer(&amaudio_out.timer, jiffies + 1);
#else
    amaudio_tmpbuf_out = (char*)kzalloc((amaudio->out_size), GFP_KERNEL);
    if(amaudio_tmpbuf_out == 0){
      printk("amaudio temp out buf alloc failed\n");
      goto error;
    }

    audout_irq_alloced = 0;
    if(amaudio_in_started){
      WRITE_MPEG_REG_BITS(AIU_MEM_I2S_MASKS,0, 16, 16);
      if(request_irq(IRQ_OUT, amaudio_out_callback, IRQF_SHARED, "audio_out",&amaudio_out)){
        kfree(amaudio_tmpbuf_out);
        amaudio_tmpbuf_out = 0;
        printk("audio_out irq request failed\n");
        goto error;
      }
      int_out_enable = 1;
      audout_irq_alloced = 1;
    }  
    amaudio_out_started = 1;
#endif    
  }else if(iminor(inode) == 1){// audio in
	printk("open audio in: start=%x,hwptr=%x\n", amaudio->in_start, get_audin_ptr());
	amaudio->type = 1;
    in_error = 0;
    in_error_flag = 0;
    if(audio_in_buf_ready == 0){
      printk("ALSA record not ready, please try again!!!\n");
      goto error;
    }

    amaudio_inbuf.out_start = (unsigned int)kzalloc((amaudio->in_size/2), GFP_KERNEL);
    if(amaudio_inbuf.out_start == 0){
      printk("mallolc failed\n");
      goto error;
    }
    amaudio_inbuf.in_start = amaudio_inbuf.out_start;
    amaudio_inbuf.out_size = amaudio->in_size/2;
    amaudio_inbuf.in_size = amaudio->in_size/2;
    amaudio_inbuf.out_wr_ptr = 0;
    amaudio_inbuf.out_rd_ptr = 0;
    amaudio_inbuf.in_rd_ptr = 0;
    amaudio_inbuf.in_wr_ptr = 0;
    amaudio_inbuf.level = 0;

    amaudio_tmpbuf_in = (char*)kzalloc((amaudio->in_size/2), GFP_KERNEL);
    if(amaudio_tmpbuf_in == 0){
      printk("amaudio temp buf alloc failed\n");
      goto error;
    }
#if DEBUG_DUMP
    dump_buf = (unsigned short*)kzalloc(dump_size*sizeof(short), GFP_KERNEL);
    if(dump_buf == 0){
      printk("malloc dump buf error\n");
    }
#endif

#if 0    
	amaudio_in.timer.function = &amaudio_in_callback;
    amaudio_in.timer.data = (unsigned long)(&amaudio_in);
    init_timer(&amaudio_in.timer);
    mod_timer(&amaudio_in.timer, jiffies + 1);
#else
#if ((defined CONFIG_SND_AML_M6)||(defined CONFIG_SND_AML_M3))
	WRITE_MPEG_REG_BITS(AUDIN_INT_CTRL, 0, 1, 1);
#else
	WRITE_MPEG_REG_BITS(AUDIN_FIFO0_CTRL, 0, 18, 1);
#endif
    tmp = get_audin_ptr();
    tmp =  READ_MPEG_REG(AUDIN_FIFO0_START)+ tmp + 1920;
    if(tmp >= READ_MPEG_REG(AUDIN_FIFO0_END))tmp -= amaudio->in_size;
    WRITE_MPEG_REG(AUDIN_FIFO0_INTR, tmp);
    
    audio_in_int_cnt = 0;

    if(request_irq(INT_AUDIO_IN, amaudio_in_callback, IRQF_SHARED, "audio_in", &amaudio_in)){
      printk("audio_in irq request failed\n");
      kfree(amaudio_tmpbuf_in);
      amaudio_tmpbuf_in = 0;
      goto error;
    }
    int_in_enable = 1;
    amaudio_in_started = 1;
#endif    
  }else if(iminor(inode) == 2){						// audio control
  	aprint("open audio control\n");
	amaudio->type = 2;
  }else if(iminor(inode) == 3){						// audio effect control
  	//printk("open audio effect control\n");
	amaudio->type = 3;
  } 
  else if(iminor(inode) == 4){			//audio utils
  	printk("open audio utils control\n");
	amaudio->type = 4;
  }
  else {
  	printk("err,this amaudio inode not implement yet\n");
	return -EINVAL;
  }
  file->private_data = amaudio;
  file->f_op = this->fops;
  return 0;
error:  
  kfree(amaudio);
  return 0;
}
static int amaudio_release(struct inode *inode, struct file *file)
{
	amaudio_t * amaudio = (amaudio_t *)file->private_data;

    if(iminor(inode) == 0){
#if 0
      del_timer_sync(&amaudio_out.timer);
#else
      if(audout_irq_alloced){
        free_irq(IRQ_OUT, &amaudio_out);
        audout_irq_alloced = 0;
      }
#endif      
 
      kfree(amaudio_tmpbuf_out);
      amaudio_tmpbuf_out = 0;
      direct_audio_flag = DIRECT_AUDIO_OFF;
      amaudio_out_started = 0;
      int_out_enable = 0;
    }else if(iminor(inode) == 1){
#if 0
      del_timer_sync(&amaudio_in.timer);
#else
      free_irq(INT_AUDIO_IN, &amaudio_in);
#endif      
    
      direct_audio_flag = DIRECT_AUDIO_OFF;
      kfree((void*)amaudio_inbuf.out_start);
      kfree(amaudio_tmpbuf_in);
      amaudio_tmpbuf_in = 0;
      amaudio_in_started = 0;
      int_in_enable = 0;
#if DEBUG_DUMP
      kfree(dump_buf);
      dump_buf = 0;
#endif      
    }
  if(amaudio)
	  kfree(amaudio);
	return 0;
}
static long amaudio_ioctl(struct file *file,
                        unsigned int cmd, unsigned long arg)
{
	s32 r = 0;
	u32 reg;
	amaudio_t * amaudio = (amaudio_t *)file->private_data;
    switch(cmd){
		case AMAUDIO_IOC_GET_I2S_OUT_SIZE:
			if(audio_out_buf_ready){
				r = READ_MPEG_REG(AIU_MEM_I2S_END_PTR) - READ_MPEG_REG(AIU_MEM_I2S_START_PTR) + 64;
			}else{
				r = -EINVAL;
			}
			break;
		case AMAUDIO_IOC_GET_I2S_OUT_PTR:
			if(audio_out_buf_ready){
				r = read_i2s_rd_ptr() - READ_MPEG_REG(AIU_MEM_I2S_START_PTR);
			}else{
				r = -EINVAL;
			}
			break;
		case AMAUDIO_IOC_SET_I2S_OUT_RD_PTR:
			if(audio_out_buf_ready){
				if(arg < 0 || arg > (READ_MPEG_REG(AIU_MEM_I2S_END_PTR) - READ_MPEG_REG(AIU_MEM_I2S_START_PTR)+64)){
					r = -EINVAL;
				}else{
					amaudio->out_rd_ptr = arg;
				}
			}else{
				r = -EINVAL;
			}
			break;
        case AMAUDIO_IOC_GET_I2S_OUT_RD_PTR:
            r = amaudio->out_rd_ptr;
            break;
        case AMAUDIO_IOC_SET_I2S_OUT_WR_PTR:
			if(audio_out_buf_ready){
				if(arg < 0 || arg > (READ_MPEG_REG(AIU_MEM_I2S_END_PTR) - READ_MPEG_REG(AIU_MEM_I2S_START_PTR)+64)){
					r = -EINVAL;
				}else{
					amaudio->out_wr_ptr = arg;
				}
			}else{
				r = -EINVAL;
			}
			break;
        case AMAUDIO_IOC_GET_I2S_OUT_WR_PTR:
            r = amaudio->out_wr_ptr;
            break;
		case AMAUDIO_IOC_GET_I2S_IN_SIZE:
			if(audio_in_buf_ready){
               r = amaudio_inbuf.in_size;
			}else{
				r = -EINVAL;
			}
			break;
		case AMAUDIO_IOC_GET_I2S_IN_PTR:
			if(audio_in_buf_ready){
              r = amaudio_inbuf.out_wr_ptr; /* should be a hw pointer */
            }
			else{
				r = -EINVAL;
			}
			break;
		case AMAUDIO_IOC_SET_I2S_IN_RD_PTR:
			if(audio_in_buf_ready){
				if(arg < 0 || arg > (READ_MPEG_REG(AUDIN_FIFO0_END)- READ_MPEG_REG(AUDIN_FIFO0_START)+8)){
					r = -EINVAL;
				}else{
					amaudio_inbuf.in_rd_ptr = arg;
				}
			}else{
				r = -EINVAL;
			}
			break;
        case AMAUDIO_IOC_GET_I2S_IN_RD_PTR:
            r = amaudio_inbuf.in_rd_ptr;
            break;
        case AMAUDIO_IOC_SET_I2S_IN_WR_PTR:
			if(audio_in_buf_ready){
				if(arg < 0 || arg > (READ_MPEG_REG(AUDIN_FIFO0_END)- READ_MPEG_REG(AUDIN_FIFO0_START)+8)){
					r = -EINVAL;
				}else{
					amaudio_inbuf.in_wr_ptr = arg;
				}
			}else{
				r = -EINVAL;
			}
			break;
        case AMAUDIO_IOC_GET_I2S_IN_WR_PTR:
            r = amaudio_inbuf.in_wr_ptr;
            break;
        case AMAUDIO_IOC_SET_I2S_IN_MODE:
            if(arg < 0 || arg > 1){
              return -EINVAL;
            }
            amaudio->in_op_mode = arg;
            break;
        case AMAUDIO_IOC_SET_I2S_OUT_MODE:
            if(arg < 0 || arg > 1){
              return -EINVAL;
            }
            amaudio->out_op_mode = arg;
            break;
		case AMAUDIO_IOC_SET_LEFT_MONO:
			audio_i2s_swap_left_right(1);
			break;
		case AMAUDIO_IOC_SET_RIGHT_MONO:
			audio_i2s_swap_left_right(2);
			break;
		case AMAUDIO_IOC_SET_STEREO:
			audio_i2s_swap_left_right(0);
			break;
		case AMAUDIO_IOC_SET_CHANNEL_SWAP:
			reg = read_i2s_mute_swap_reg();
			if(reg & 0x3)
				audio_i2s_swap_left_right(0);
			else
				audio_i2s_swap_left_right(3);
			break;
		case AMAUDIO_IOC_DIRECT_AUDIO:
			direct_audio_ctrl(arg);
			break;		
        case AMAUDIO_IOC_DIRECT_LEFT_GAIN:
            direct_audio_left_gain(arg);
            break;
        case AMAUDIO_IOC_DIRECT_RIGHT_GAIN:
            direct_audio_right_gain(arg);
            break;
		default:
			break;
		
	};
	return r;
}

static int amaudio_utils_open(struct inode *inode, struct file *file)
{
  return 0;
}
static int amaudio_utils_release(struct inode *inode, struct file *file)
{   
  return 0;
}
static long amaudio_utils_ioctl(struct file *file,
                        unsigned int cmd, unsigned long arg)
{
    s32 r = 0;
    u32 reg;
    //amaudio_t * amaudio = (amaudio_t *)file->private_data;
    switch(cmd) {
        case AMAUDIO_IOC_SET_LEFT_MONO:
            audio_i2s_swap_left_right(1);
            break;
        case AMAUDIO_IOC_SET_RIGHT_MONO:
            audio_i2s_swap_left_right(2);
            break;
        case AMAUDIO_IOC_SET_STEREO:
            audio_i2s_swap_left_right(0);
            break;
        case AMAUDIO_IOC_SET_CHANNEL_SWAP:
            reg = read_i2s_mute_swap_reg();
            if(reg & 0x3)
                audio_i2s_swap_left_right(0);
            else
            	audio_i2s_swap_left_right(3);
            break;
        case AMAUDIO_IOC_DIRECT_AUDIO:
            direct_audio_ctrl(arg);
            break;		
        case AMAUDIO_IOC_DIRECT_LEFT_GAIN:
            direct_audio_left_gain(arg);
            break;
        case AMAUDIO_IOC_DIRECT_RIGHT_GAIN:
            direct_audio_right_gain(arg);
            break;

        case AMAUDIO_IOC_START_LINE_IN:
            // select audio codec output as I2S source
         //   WRITE_MPEG_REG(AUDIN_SOURCE_SEL, (1<<0));
            // prepare aiu
        //    audio_in_i2s_set_buf(aml_pcm_capture_start_phy, aml_pcm_capture_buf_size*2);
            memset((void*)aml_i2s_capture_start_addr,0,aml_i2s_capture_buf_size*2);
            // prepare codec
     //       aml_linein_start();
            // trigger aiu
            printk("i2s in enable\n");
            audio_in_i2s_enable(1);
            break;
        case AMAUDIO_IOC_STOP_LINE_IN:
            // stop aiu
            printk("i2s in disable\n");            
            audio_in_i2s_enable(0);
            // stop codec
     //       aml_linein_stop();
            break;
#if 0			
        case AMAUDIO_IOC_START_HDMI_IN:
            // set audio in source to hdmi in
            WRITE_MPEG_REG(AUDIN_SOURCE_SEL,    (1<<4) | (2 << 0));    // Select HDMI RX output as AUDIN source
            // prepare aiu
            audio_in_i2s_set_buf(aml_pcm_capture_start_phy, aml_pcm_capture_buf_size*2);
            memset((void*)aml_pcm_capture_start_addr,0,aml_pcm_capture_buf_size*2);            
            // trigger aiu
            audio_in_i2s_enable(1);
            break;
        case AMAUDIO_IOC_STOP_HDMI_IN:
            // stop aiu
            audio_in_i2s_enable(0);
            // set audio in source to line in
            WRITE_MPEG_REG(AUDIN_SOURCE_SEL, (1<<0)); // select audio codec output as I2S source
            break;
#endif			
        case AMAUDIO_IOC_GET_RESAMPLE_ENA:
            put_user(enable_resample_flag,(__u32 __user *)arg);
            break;
        case AMAUDIO_IOC_SET_RESAMPLE_ENA:
            enable_resample_flag = arg;
            break;
        case AMAUDIO_IOC_SET_RESAMPLE_TYPE:
            resample_type_flag = arg;
            break; 
		case AMAUDIO_IOC_SET_RESAMPLE_DELTA:
			resample_delta=arg;
            break;
        case AMAUDIO_IOC_GET_RESAMPLE_DELTA:
            put_user(resample_delta,(__s32 __user *)arg);
            printk("set resample_delta=%d\n ",resample_delta);
            break;
        default:
        	break;
    };
    return r;
}
static const struct file_operations amaudio_fops = {
  .owner    =   THIS_MODULE,
  .open     =   amaudio_open,
  .unlocked_ioctl    =   amaudio_ioctl,
  .release  =   amaudio_release,
};


static ssize_t show_direct_flag(struct class* class, struct class_attribute* attr,
    char* buf)
{
  return sprintf(buf, "DIRECT AUDIO %d\n", direct_audio_flag);
}

static ssize_t store_direct_flag(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  if(buf[0] == '0'){
    direct_audio_flag = DIRECT_AUDIO_OFF;
  }else if(buf[0] == '1'){
    direct_audio_flag = DIRECT_AUDIO_ON;
  }
  return count;
}

static ssize_t show_music_mix(struct class* class, struct class_attribute* attr,
    char* buf)
{
  return sprintf(buf, "MUSIC MIX %s\n", music_mix_flag? "ON": "OFF");
}

static ssize_t store_music_mix(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  if(buf[0] == '0'){
    music_mix_flag = 0;
  }else if(buf[0] == '1'){
    music_mix_flag = 1;
  }
  return count;
}

static ssize_t show_mic_mix(struct class* class, struct class_attribute* attr,
    char* buf)
{
  return sprintf(buf, "MIC MIX %s\n", mic_mix_flag? "ON": "OFF");
}

static ssize_t store_mic_mix(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  if(buf[0] == '0'){
    mic_mix_flag = 0;
  }else if(buf[0] == '1'){
    mic_mix_flag = 1;
  }
  return count;
}

static ssize_t show_alsa_out(struct class* class, struct class_attribute* attr,
    char* buf)
{
  return sprintf(buf, "ALSA OUT %s\n", aml_i2s_playback_enable? "ON": "OFF");
}

static ssize_t store_alsa_out(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  if(buf[0] == '0'){
    aml_i2s_playback_enable = 0;
  }else if(buf[0] == '1'){
    aml_i2s_playback_enable = 1;
  }
  return count;
}
static ssize_t show_enable_debug(struct class* class, struct class_attribute* attr,
    char* buf)
{
  return sprintf(buf, "AMAUDIo DEBUG %s\n", enable_debug? "ON": "OFF");
}

static ssize_t store_enable_debug(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  if(buf[0] == '0'){
    enable_debug = 0;
  }else if(buf[0] == '1'){
    enable_debug = 1;
  }
  return count;
}
extern void audio_in_i2s_enable(int);
static ssize_t show_enable_dump(struct class* class, struct class_attribute* attr,
    char* buf)
{
  ssize_t ret = sprintf(buf, "AMAUDIO DUMP:\n");
#if DEBUG_DUMP
  int i, j;
  printk("AMAUDIO DUMP HardWBuf Start: in hwptr=%d, in rd=%d, out hwptr=%d, out wr=%d\n", get_audin_ptr(), amaudio_in.in_rd_ptr, get_audout_ptr(), amaudio_out.out_wr_ptr);
  audio_in_i2s_enable(0);
  if (dump_buf) {
      for(i=0; i< dump_size; i+=8){
        for(j=0; j<8; j++){
          printk("%04x,",dump_buf[i+j]);
        }
        printk("\n");
      }
  }
  printk("AMAUDIo DUMp FINISHED\n\n\n");

  if (amaudio_in.in_start) {
      for(i=0; i< amaudio_in.in_size/4; i+=8){
        for(j=0; j<8; j++){
          printk("%08x,", *((unsigned *)(amaudio_in.in_start) + i + j));
        }
        printk("\n");
      }
  }

  printk("OUTPUT\n");
  if (amaudio_in.out_start) {
      for(i=0; i<amaudio_out.out_size/4; i+= 8){
        for(j=0; j< 8; j++){
          printk("%08x,", *((unsigned*)(amaudio_out.out_start) + i + j));
        }
        printk("\n");
      }
  }
  printk("Hardware Buf finished: audio in error: %d, error flag = %d\n", in_error, in_error_flag);
  audio_in_i2s_enable(1);
#endif  
  return ret;
}

static ssize_t store_enable_dump(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  //unsigned long flags;
  
  unsigned int tmp = 0;

  if(buf[0] == '0'){
    tmp = 0;
  }else if(buf[0] == '1'){
    tmp = 1;
  }
  
  enable_debug_dump = tmp;
  
  return count;
}

static ssize_t show_audio_channels_mask(struct class* class, struct class_attribute* attr, char* buf)
{
    ssize_t ret = 0;

    ret = sprintf(buf, "echo l/r/s/c to /sys/class/amaudio/audio_channels_mask file to mask audio output. \n"
                         " l : left channel mono output. \n"
                         " r : right channel mono output. \n"
                         " s : stereo output. \n"
                         " c : swap left and right channels.\n");

    return ret;
}

static ssize_t store_audio_channels_mask(struct class* class, struct class_attribute* attr, const char* buf, size_t count)
{
    u32 reg;

    switch(buf[0]) {
        case 'l':
            audio_i2s_swap_left_right(1);
            break;

        case 'r':
            audio_i2s_swap_left_right(2);
            break;

        case 's':
            audio_i2s_swap_left_right(0);
            break;

        case 'c':
            reg = read_i2s_mute_swap_reg();
            if(reg & 0x3)
                audio_i2s_swap_left_right(0);
            else
                audio_i2s_swap_left_right(3);
            break;

        default:
            printk("unknow command!\n");
    }

    return count;
}


static ssize_t amaudio_runtime_show(struct class* class, struct class_attribute* attr,
    char* buf)
{
  ssize_t ret = 0;
  unsigned in_hwptr = get_audin_ptr();
  unsigned out_hwptr = get_audout_ptr();

  ret = sprintf(buf, "amaudio runtime info:\n"
                     "  i2s out hw ptr:\t%d\n"    
                     "  i2s in  hw ptr:\t%d\n"    
                     "  direct wr     :\t%d\n"
                     "  direct rd     :\t%d\n"
                     "  temp buf wr   :\t%d\n"
                     "  temp buf rd   :\t%d\n"
                     "  music mix wr  :\t%d\n"
                     "  music mix rd  :\t%d\n",
                    out_hwptr, in_hwptr, 
                    amaudio_out.out_wr_ptr, amaudio_in.in_rd_ptr,
                    amaudio_inbuf.out_wr_ptr, amaudio_inbuf.out_rd_ptr, 
                    music_wr_ptr, amaudio_inbuf.in_rd_ptr );
  return ret;
}

//--------------------------------------------
static ssize_t show_enable_resample(struct class* class, struct class_attribute* attr,
    char* buf)
{
  return sprintf(buf, "%s\n", enable_resample_flag? "ON": "OFF");
}

static ssize_t store_enable_resample(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  if(buf[0] == '0'){
    enable_resample_flag = 0;
	timestamp_enable_resample_flag = 0;
  }else if(buf[0] == '1'){
    enable_resample_flag = 1;
	timestamp_enable_resample_flag = 1;
  }
  return count;
}

static ssize_t show_resample_type(struct class* class, struct class_attribute* attr,
    char* buf)
{
     if(resample_type_flag==0){      //0-->no resample  processing
         return sprintf(buf, "NO\n");
     }else if(resample_type_flag==1){//1-->down resample processing
         return sprintf(buf, "DW\n");
     }else if(resample_type_flag==2){//2-->up resample processing
         return sprintf(buf, "UP\n");
     }else{                          //other-->invalid resample type flag
         return sprintf(buf, "IR\n");
     }
}

static ssize_t store_resample_type(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  if(buf[0] == '0'){ 
    resample_type_flag = 0;  //0-->no resample  processing
    timestamp_resample_type_flag = 0;
  }else if(buf[0] == '1'){     
    resample_type_flag = 1;  //1-->down resample processing
    timestamp_resample_type_flag = 1;
  }else if(buf[0] == '2'){
    resample_type_flag = 2;  //2-->up resample processing
    timestamp_resample_type_flag = 2;
  }
  return count;
}

static ssize_t show_resample_delta(struct class* class, struct class_attribute* attr,
    char* buf)
{
  return sprintf(buf, "%d\n", resample_delta);
}

static ssize_t store_resample_delta(struct class* class, struct class_attribute* attr,
    const char* buf, size_t count)
{
  int val = 0;
  val = simple_strtoul(buf, NULL, 10);
  printk("resample delta set to %d\n", val);
  return count;
}

static ssize_t dac_mute_const_show(struct class*cla, struct class_attribute* attr, char* buf)
{
  char* pbuf = buf;
  pbuf += sprintf(pbuf, "dac mute const val  0x%x\n", dac_mute_const);
  return (pbuf-buf);
}
static ssize_t dac_mute_const_store(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  unsigned val = dac_mute_const;
  if(buf[0])
  	val=simple_strtoul(buf, NULL, 16);	
  if(val == 0 || val == 0x800000)
  	dac_mute_const = val;
  printk("dac mute const val set to 0x%x\n", val);
  return count;
}

//--------------------------------------------
static ssize_t output_enable_show(struct class* class, struct class_attribute* attr,
	char* buf)
{
//why limit this condition of iec958 buffer size bigger than 128,
//because we begin to use a 128 bytes zero playback mode of 958 output when alsa driver is not called by user space to
//  avoid some noise.This mode should must seperate this case with normal playback case to avoid one risk:
// when EOF,the last three seconds this is no audio pcm decoder to output.the zero playback mode is triggered,
//this cause the player has no chance to  trigger the exit condition
     unsigned iec958_size = READ_MPEG_REG(AIU_MEM_IEC958_END_PTR)-READ_MPEG_REG(AIU_MEM_IEC958_START_PTR);
     iec958_size += 64;	 
     return sprintf(buf, "%d\n", if_audio_out_enable()||(if_958_audio_out_enable()&&iec958_size > 128)); 
}
extern unsigned audioin_mode;
enum {
	I2SIN_MASTER_MODE = 0,
	I2SIN_SLAVE_MODE  =   1<<0,
	SPDIFIN_MODE   = 1<<1,
};
static ssize_t record_type_store(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  if(buf[0] == '0'){
    audioin_mode = I2SIN_MASTER_MODE;
  }else if(buf[0] == '1'){
    audioin_mode = I2SIN_SLAVE_MODE;
  }
  else if(buf[0] == '2'){
     audioin_mode = SPDIFIN_MODE; 
  }
  return count;
}

static ssize_t record_type_show(struct class* class, struct class_attribute* attr,
    char* buf)
{
     if(audioin_mode&I2SIN_MASTER_MODE){      //mic 
         return sprintf(buf, "i2s in master mode for built in mic\n");
     }else if(audioin_mode&SPDIFIN_MODE){//spdif in mode 
         return sprintf(buf, "spdif in mode \n");
     }else if(audioin_mode&I2SIN_SLAVE_MODE){//i2s in slave
         return sprintf(buf, "i2s in slave mode \n");
     }else{
         return sprintf(buf, "audioin_mode can't match mode\n");
     }
}


static ssize_t store_debug(struct class* class, struct class_attribute* attr,  const char* buf, size_t count )
{
    if(strncmp(buf, "chstatus_set", 12)==0)
     {     
               WRITE_MPEG_REG(AIU_958_VALID_CTRL,0);//disable 958 invalid bit			
               WRITE_MPEG_REG(AIU_958_CHSTAT_L0, 0x1900);		
               WRITE_MPEG_REG(AIU_958_CHSTAT_R0, 0x1900);
     }else if(strncmp(buf, "chstatus_off", 12)==0){
            	WRITE_MPEG_REG(AIU_958_VALID_CTRL,3);//enable 958 invalid bit				
            	WRITE_MPEG_REG(AIU_958_CHSTAT_L0, 0x1902);		
              WRITE_MPEG_REG(AIU_958_CHSTAT_R0, 0x1902);	 
      }
      return count;
}

static ssize_t show_debug(struct class* class, struct class_attribute* attr,  char* buf)
{
      return 0;
}


static struct class_attribute amaudio_attrs[]={
  __ATTR(enable_direct_audio,  S_IRUGO | S_IWUSR, show_direct_flag, store_direct_flag),
  __ATTR(enable_music_mix, S_IRUGO | S_IWUSR, show_music_mix, store_music_mix),
  __ATTR(enable_mic_mix, S_IRUGO | S_IWUSR, show_mic_mix, store_mic_mix),
  __ATTR(enable_alsa_out, S_IRUGO | S_IWUSR, show_alsa_out, store_alsa_out),
  __ATTR(enable_debug_print, S_IRUGO | S_IWUSR, show_enable_debug, store_enable_debug),
  __ATTR(enable_debug_dump, S_IRUGO | S_IWUSR, show_enable_dump, store_enable_dump),
  __ATTR_RO(amaudio_runtime),
  __ATTR(audio_channels_mask, S_IRUGO | S_IWUSR | S_IWGRP, show_audio_channels_mask, store_audio_channels_mask),
  __ATTR(enable_resample, S_IRUGO | S_IWUSR | S_IWGRP, show_enable_resample, store_enable_resample),
  __ATTR(resample_type, S_IRUGO | S_IWUSR | S_IWGRP, show_resample_type, store_resample_type),
  __ATTR(resample_delta, S_IRUGO | S_IWUSR, show_resample_delta, store_resample_delta),
  __ATTR(dac_mute_const, S_IRUGO | S_IWUSR, dac_mute_const_show, dac_mute_const_store),
  __ATTR_RO(output_enable),
  __ATTR(record_type, S_IRUGO | S_IWUSR, record_type_show, record_type_store),  
   __ATTR(debug, S_IRUGO | S_IWUSR | S_IWGRP, show_debug, store_debug),
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
static int __init amaudio_init(void)
{
  int ret = 0;
  int i=0;
  amaudio_port_t* ap;

  ret = alloc_chrdev_region(&amaudio_devno, 0, AMAUDIO_DEVICE_COUNT, AMAUDIO_DEVICE_NAME);
  if(ret < 0){
    aprint(KERN_ERR"amaudio: faild to alloc major number\n");
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
    aprint(KERN_ERR"amaudio: failed to allocate memory\n");
    ret = -ENOMEM;
    goto err2;
  }
  // connect the file operation with cdev
  cdev_init(amaudio_cdevp, &amaudio_fops);
  amaudio_cdevp->owner = THIS_MODULE;
  // connect the major/minor number to cdev
  ret = cdev_add(amaudio_cdevp, amaudio_devno, AMAUDIO_DEVICE_COUNT);
  if(ret){
    aprint(KERN_ERR "amaudio:failed to add cdev\n");
    goto err3;
  } 
  for(ap = &amaudio_ports[0], i=0; i< AMAUDIO_DEVICE_COUNT; ap++,  i++){    
    ap->dev = device_create(amaudio_clsp, NULL, MKDEV(MAJOR(amaudio_devno),i), NULL,amaudio_ports[i].name);
    if(IS_ERR(ap->dev)){
      aprint(KERN_ERR"amaudio: failed to create amaudio device node\n");
      goto err4;
    }
  }
  spin_lock_init(&amaudio_lock);
  spin_lock_init(&amaudio_clk_lock);
  aprint(KERN_INFO"amaudio: device %s created\n", AMAUDIO_DEVICE_NAME);
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

static void __exit amaudio_exit(void)
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
void aml_device_destroy(struct class *pclass, dev_t devt)
{
	device_destroy(pclass,devt);
}
void aml_class_destroy(struct class *cls)
{
	class_destroy(cls);
}
struct device *aml_device_create(struct class *pclass, struct device *parent,
			     dev_t devt, void *drvdata, const char *fmt, ...)
{
	struct device *dev;
	va_list vargs;	
	va_start(vargs, fmt);
	dev=  device_create(pclass, parent,devt, drvdata, fmt, vargs);
	va_end(vargs);
	return dev;	
}
int aml_class_create_file(struct class *cls, const struct class_attribute *attr)
{
	return class_create_file(cls,attr);

}
struct class *aml_class_create(struct module *owner, const char *name){
	return class_create(owner,name);
}
EXPORT_SYMBOL(aml_device_destroy);
EXPORT_SYMBOL(aml_class_destroy);
EXPORT_SYMBOL(aml_device_create);
EXPORT_SYMBOL(aml_class_create_file);
EXPORT_SYMBOL(aml_class_create);
module_init(amaudio_init);
module_exit(amaudio_exit);
