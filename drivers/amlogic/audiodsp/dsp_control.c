#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
//include <asm/arch/am_regs.h>
#include <mach/am_regs.h>
//#include <asm/bsp.h>
#include <linux/dma-mapping.h>

#include "dsp_microcode.h"
#include "audiodsp_module.h"
#include "dsp_control.h"
#include "dsp_codec.h"


//#include <asm/dsp/dsp_register.h>
#include <linux/amlogic/amports/dsp_register.h>


#include "dsp_mailbox.h"
#include <linux/delay.h>
#include <linux/clk.h>

#define MIN_CACHE_ALIGN(x)	(((x-4)&(~0x1f)))
#define MAX_CACHE_ALIGN(x)	((x+0x1f)&(~0x1f))
#define MAX_STREAM_BUF_MEM_SIZE  (32*1024)

extern unsigned IEC958_mode_raw;
extern unsigned IEC958_mode_codec;
extern unsigned audioin_mode;

int decopt = 0x0000fffb;
int subid  = 0x0;

 //M8   MEDIA_CPU_CTL
    // reg   [31:0]    media_cpu_cntl;
    // assign  media_cpu_remap      = media_cpu_cntl[31:20];
    // wire    media_cpu_remap0     = media_cpu_cntl[16]; // 1=remap address 0 to DDR instead of flash or boot ROM
    // wire    media_cpu_start_a    = media_cpu_cntl[0];  // 0: run on reset,  1: halt on reset
    // wire    media_cpu_ctrl_start = media_cpu_cntl[1];  // which is used to start the processor that is set to halt on reset
#define RESET_AUD_ARC	(1<<13)
static void	enable_dsp(int flag)
{
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	//power down the media cpu
	if(!flag)
		 WRITE_CBUS_REG_BITS(HHI_MEM_PD_REG0,3,0,2);
	//reset 
 	SET_MPEG_REG_MASK(RESET1_REGISTER, RESET_AUD_ARC);	
	if(flag)
	{
		//power on media cpu
		WRITE_CBUS_REG_BITS(HHI_MEM_PD_REG0,0,0,2);	
		//enable 
		SET_MPEG_REG_MASK(MEDIA_CPU_CTL, 3);		
	}
#else
	 if(!flag)
	  	 CLEAR_MPEG_REG_MASK(MEDIA_CPU_CTL, 1);
	 SET_MPEG_REG_MASK(RESET2_REGISTER, RESET_AUD_ARC);	
        if (flag) {
		    SET_MPEG_REG_MASK(MEDIA_CPU_CTL, 1);
		    CLEAR_MPEG_REG_MASK(MEDIA_CPU_CTL, 1);
	}
#endif		
}

void halt_dsp( struct audiodsp_priv *priv)
{
	int i = 0;
	if(DSP_RD(DSP_STATUS)==DSP_STATUS_RUNING)
		{
#ifndef AUDIODSP_RESET
		int i;
		dsp_mailbox_send(priv,1,M2B_IRQ0_DSP_SLEEP,0,0,0);
        for(i = 0; i< 100;i++)
            {
                if(DSP_RD(DSP_STATUS)== DSP_STATUS_SLEEP)
                    break;
		        msleep(1);/*waiting arc2 sleep*/
            }
        if(i == 100)
           DSP_PRNT("warning: dsp is not sleeping when call dsp_stop\n"); 
#else
	for(i = 0; i <  10;i++){
		dsp_mailbox_send(priv,1,M2B_IRQ0_DSP_HALT,0,0,0);
		msleep(1);/*waiting arc2 self-halt*/
		if(DSP_RD(DSP_STATUS) ==DSP_STATUS_HALT)
			break;
	}
	if( i == 10){
		DSP_PRNT("warning,dsp self-halt time out \n");
	}
#endif
       }
#ifdef AUDIODSP_RESET	
	if(DSP_RD(DSP_STATUS)!=DSP_STATUS_RUNING)
	{
		DSP_WD(DSP_STATUS, DSP_STATUS_HALT);
		return ;
	}
#endif
    if(!priv->dsp_is_started){

	    enable_dsp(0);/*hardware halt the cpu*/
           DSP_WD(DSP_STATUS, DSP_STATUS_HALT);
           priv->last_stream_fmt=-1;/*mask the stream format is not valid*/
    }   
    else
        DSP_WD(DSP_STATUS, DSP_STATUS_SLEEP);
	
}
void reset_dsp( struct audiodsp_priv *priv)
{
    halt_dsp(priv);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    CLEAR_MPEG_REG_MASK(MEDIA_CPU_CTL, (0xfff << 20));
    SET_MPEG_REG_MASK(MEDIA_CPU_CTL, ((AUDIO_DSP_START_PHY_ADDR)>> 20) << 20);
    SET_MPEG_REG_MASK(MEDIA_CPU_CTL, 1<<16);
#else
    CLEAR_MPEG_REG_MASK(MEDIA_CPU_CTL, (0xfff << 4));
    SET_MPEG_REG_MASK(MEDIA_CPU_CTL, ((AUDIO_DSP_START_PHY_ADDR)>> 20) << 4);
#endif	
// decode option    
    if(audioin_mode &2){
		decopt &= ~(1<<6);
    }
    if(IEC958_mode_codec){
      if(IEC958_mode_codec == 4){//dd+
		DSP_WD(DSP_DECODE_OPTION, decopt|(3<<30));
      }else{
		DSP_WD(DSP_DECODE_OPTION, decopt|(1<<31));//dd,dts
      }
    }
	else{
		DSP_WD(DSP_DECODE_OPTION, decopt&(~(1<<31)));
	}
    
    DSP_WD(DSP_CHIP_SUBID, subid);

    printk("reset dsp : dec opt=%lx, subid=%lx\n", DSP_RD(DSP_DECODE_OPTION), DSP_RD(DSP_CHIP_SUBID));
    if(!priv->dsp_is_started){
        DSP_PRNT("dsp reset now\n");
        enable_dsp(1);
        }
    else{
       	dsp_mailbox_send(priv,1,M2B_IRQ0_DSP_WAKEUP,0,0,0);
        DSP_WD(DSP_STATUS, DSP_STATUS_WAKEUP);
        msleep(1);/*waiting arc625 run again */

    }

    return;    
}
static inline int dsp_set_stack( struct audiodsp_priv *priv)
{
      dma_addr_t buf_map;
	if(priv->dsp_stack_start==0)
		priv->dsp_stack_start=(unsigned long)kmalloc(priv->dsp_stack_size,GFP_KERNEL);
	if(priv->dsp_stack_start==0)
		{
		DSP_PRNT("kmalloc error,no memory for audio dsp stack\n");
		return -ENOMEM;
		}
	memset((void*)priv->dsp_stack_start,0,priv->dsp_stack_size);
        buf_map = dma_map_single(NULL, (void *)priv->dsp_stack_start, priv->dsp_stack_size, DMA_FROM_DEVICE);
	 dma_unmap_single(NULL, buf_map,  priv->dsp_stack_size, DMA_FROM_DEVICE);

	DSP_WD(DSP_STACK_START,MAX_CACHE_ALIGN(ARM_2_ARC_ADDR_SWAP(priv->dsp_stack_start)));
	DSP_WD(DSP_STACK_END,MIN_CACHE_ALIGN(ARM_2_ARC_ADDR_SWAP(priv->dsp_stack_start)+priv->dsp_stack_size));
	DSP_PRNT("DSP statck start =%#lx,size=%#lx\n",(ulong)ARM_2_ARC_ADDR_SWAP(priv->dsp_stack_start),priv->dsp_stack_size);
	if(priv->dsp_gstack_start==0)
		priv->dsp_gstack_start=(unsigned long)kmalloc(priv->dsp_gstack_size,GFP_KERNEL);
	if(priv->dsp_gstack_start==0)
		{
		DSP_PRNT("kmalloc error,no memory for audio dsp gp stack\n");
		kfree((void *)priv->dsp_stack_start);
		return -ENOMEM;
		}
	memset((void*)priv->dsp_gstack_start,0,priv->dsp_gstack_size);
        buf_map = dma_map_single(NULL, (void *)priv->dsp_gstack_start, priv->dsp_gstack_size, DMA_FROM_DEVICE);
	 dma_unmap_single(NULL, buf_map,  priv->dsp_gstack_size, DMA_FROM_DEVICE);
	DSP_WD(DSP_GP_STACK_START,MAX_CACHE_ALIGN(ARM_2_ARC_ADDR_SWAP(priv->dsp_gstack_start)));
	DSP_WD(DSP_GP_STACK_END,MIN_CACHE_ALIGN(ARM_2_ARC_ADDR_SWAP(priv->dsp_gstack_start)+priv->dsp_gstack_size));
	DSP_PRNT("DSP gp statck start =%#lx,size=%#lx\n",(ulong)ARM_2_ARC_ADDR_SWAP(priv->dsp_gstack_start),priv->dsp_gstack_size);
		
	return 0;
}
static inline int dsp_set_heap( struct audiodsp_priv *priv)
{
      dma_addr_t buf_map;
	if(priv->dsp_heap_size==0)
		return 0;
	if(priv->dsp_heap_start==0)
		priv->dsp_heap_start=(unsigned long)kmalloc(priv->dsp_heap_size,GFP_KERNEL);
	if(priv->dsp_heap_start==0)
		{
		DSP_PRNT("kmalloc error,no memory for audio dsp dsp_set_heap\n");
		return -ENOMEM;
		}
	memset((void *)priv->dsp_heap_start,0,priv->dsp_heap_size);
       buf_map = dma_map_single(NULL, (void *)priv->dsp_heap_start, priv->dsp_heap_size, DMA_FROM_DEVICE);
	dma_unmap_single(NULL, buf_map,  priv->dsp_heap_size, DMA_FROM_DEVICE);
	DSP_WD(DSP_MEM_START,MAX_CACHE_ALIGN(ARM_2_ARC_ADDR_SWAP(priv->dsp_heap_start)));
	DSP_WD(DSP_MEM_END,MIN_CACHE_ALIGN(ARM_2_ARC_ADDR_SWAP(priv->dsp_heap_start)+priv->dsp_heap_size));
	DSP_PRNT("DSP heap start =%#lx,size=%#lx\n",(ulong)ARM_2_ARC_ADDR_SWAP(priv->dsp_heap_start),priv->dsp_heap_size);
	return 0;
}

static inline int dsp_set_stream_buffer( struct audiodsp_priv *priv)
{
      dma_addr_t buf_map;
	if(priv->stream_buffer_mem_size==0)
		{
		DSP_WD(DSP_DECODE_OUT_START_ADDR,0);
		DSP_WD(DSP_DECODE_OUT_END_ADDR,0);
		DSP_WD(DSP_DECODE_OUT_RD_ADDR,0);
		DSP_WD(DSP_DECODE_OUT_WD_ADDR,0);
		return 0;
		}
	if(priv->stream_buffer_mem==NULL)
		priv->stream_buffer_mem=(void*)kmalloc(MAX_STREAM_BUF_MEM_SIZE,GFP_KERNEL);
	if(priv->stream_buffer_mem==NULL)
		{
		DSP_PRNT("kmalloc error,no memory for audio dsp stream buffer\n");
		return -ENOMEM;
		}
	memset((void *)priv->stream_buffer_mem,0,priv->stream_buffer_mem_size);
    	buf_map = dma_map_single(NULL, (void *)priv->stream_buffer_mem, priv->stream_buffer_mem_size, DMA_FROM_DEVICE);
	dma_unmap_single(NULL, buf_map,  priv->stream_buffer_mem_size, DMA_FROM_DEVICE);

	priv->stream_buffer_start=MAX_CACHE_ALIGN((unsigned long)priv->stream_buffer_mem);
	priv->stream_buffer_end=MIN_CACHE_ALIGN((unsigned long)priv->stream_buffer_mem+priv->stream_buffer_mem_size);
	priv->stream_buffer_size=priv->stream_buffer_end-priv->stream_buffer_start;
	if(priv->stream_buffer_size<0)
		{
		DSP_PRNT("Stream buffer set error,must more larger,mensize=%d,buffer size=%ld\n",
			priv->stream_buffer_mem_size,priv->stream_buffer_size
			);
		kfree(priv->stream_buffer_mem);
		priv->stream_buffer_mem=NULL;
		return -2;
		}
		
	DSP_WD(DSP_DECODE_OUT_START_ADDR,ARM_2_ARC_ADDR_SWAP(priv->stream_buffer_start));
	DSP_WD(DSP_DECODE_OUT_END_ADDR,ARM_2_ARC_ADDR_SWAP(priv->stream_buffer_end));
	DSP_WD(DSP_DECODE_OUT_RD_ADDR,ARM_2_ARC_ADDR_SWAP(priv->stream_buffer_start));
	DSP_WD(DSP_DECODE_OUT_WD_ADDR,ARM_2_ARC_ADDR_SWAP(priv->stream_buffer_start));
	
	DSP_PRNT("DSP stream buffer to [%#lx-%#lx]\n",(ulong)ARM_2_ARC_ADDR_SWAP(priv->stream_buffer_start),(ulong)ARM_2_ARC_ADDR_SWAP(priv->stream_buffer_end));
	return 0;
}


 int dsp_start( struct audiodsp_priv *priv, struct audiodsp_microcode *mcode)
 {
	int i;
	int res;
	mutex_lock(&priv->dsp_mutex);		
	halt_dsp(priv);
	if(priv->stream_fmt!=priv->last_stream_fmt) // remove the trick, bug fixed on dsp side
		{
		if(audiodsp_microcode_load(audiodsp_privdata(),mcode)!=0)
			{
			printk("load microcode error\n");
			res=-1;
			goto exit;
			}
		priv->last_stream_fmt=priv->stream_fmt;
		}
	if((res=dsp_set_stack(priv)))
		goto exit;
	if((res=dsp_set_heap(priv)))
		goto exit;
	if((res=dsp_set_stream_buffer(priv)))
		goto exit;
    if(!priv->dsp_is_started)
	    reset_dsp(priv);
    else{
        dsp_mailbox_send(priv,1,M2B_IRQ0_DSP_WAKEUP,0,0,0);
        msleep(1);/*waiting arc625 run again */
    }    
	priv->dsp_start_time=jiffies;
    
	for(i=0;i<1000;i++)
		{            
		if(DSP_RD(DSP_STATUS)==DSP_STATUS_RUNING)
			break;
		msleep(1);
		}
	if(i>=1000)
		{
		DSP_PRNT("dsp not running \n");
		res=-1;
		}
	else
		{
		DSP_PRNT("dsp status=%lx\n",DSP_RD(DSP_STATUS));
		priv->dsp_is_started=1;
		res=0;
		}
exit:
	mutex_unlock(&priv->dsp_mutex);		
	return res;
 }

 int dsp_stop( struct audiodsp_priv *priv)
 	{
 	mutex_lock(&priv->dsp_mutex);		
#ifdef AUDIODSP_RESET	
 	priv->dsp_is_started=0;
#endif
 	halt_dsp(priv);
	priv->dsp_end_time=jiffies;
#if 0	
	if(priv->dsp_stack_start!=0)
		kfree((void*)priv->dsp_stack_start);
	priv->dsp_stack_start=0;
	if(priv->dsp_gstack_start!=0)
		kfree((void*)priv->dsp_gstack_start);
	priv->dsp_gstack_start=0;
	if(priv->dsp_heap_start!=0)
		kfree((void*)priv->dsp_heap_start);
	priv->dsp_heap_start=0;
	if(priv->stream_buffer_mem!=NULL)
		{
		kfree(priv->stream_buffer_mem);
		priv->stream_buffer_mem=NULL;		
		}
	#endif
	mutex_unlock(&priv->dsp_mutex);	
	return 0;
 	}
int   dsp_check_status(struct audiodsp_priv *priv)
{
	//unsigned dsp_halt_score = 0;
	unsigned ablevel  = 0;
	int  pcmlevel = 0; 
	if(DSP_RD(DSP_STATUS) != DSP_STATUS_RUNING)
		return 1;	
	ablevel =READ_MPEG_REG(AIU_MEM_AIFIFO_LEVEL);
	pcmlevel = dsp_codec_get_bufer_data_len(priv);
	if((ablevel == priv->last_ablevel && ablevel > 50*1024)&& \
	    (pcmlevel == priv->last_pcmlevel && pcmlevel < 512)){
	    	priv->last_ablevel = ablevel;
		priv->last_pcmlevel = 	pcmlevel;
	    	printk("dsp not working ............\n");
	    	return 0;
	}
    	priv->last_ablevel = ablevel;
	priv->last_pcmlevel = 	pcmlevel;	
	return 1;
}

/**
 *	bit31 - digital raw output
 *	bit30 - IEC61937 pass over HDMI
 *    bit 6  -  audio in mode.
 		     00: spdif in mode
 		     01: i2s in mode
 *    bit 5 - DTS passthrough working mode
 		     00:  AIU 958 hw search raw mode 
 		     01:  PCM_RAW mode,the same as AC3/AC3+
 *    bit 3:4 - used for the communication of dsp and player tansfer decoding infomation:
 *                00: used for libplayer_end to tell dsp_end that the file end has been notreached;
 *                01: used for libplayer_end to tell dsp_end that the file end has been reached;
 *                10: used for dsp_end to tell libplayer_end that all the data in the dsp_end_buf has been decoded completely;
 *                11: reserved;
 *	bit 2 - ARC DSP print flag
 *	bit 1  - dts decoder policy select: 0:mute 1:noise
 *	bit 0  - dd/dd+ 	decoder policy select  0:mute 1:noise
 * */

static  int __init decode_option_setup(char *s)
{
    unsigned long value = 0xffffffffUL;
    if(strict_strtoul(s, 16, &value)){
      decopt = 0x0000fffb;
      return -1;
    }
    decopt = (int)value;
    return 0;
}
__setup("decopt=",decode_option_setup) ;


static int __init decode_subid_setup(char* s)
{
  unsigned long value = (unsigned long)(0);
  if(strict_strtoul(s,16,&value)){
    subid = (unsigned long)(0);
    return -1;
  }
  subid = (int)value;
  return 0;
}
__setup("meson_id=", decode_subid_setup);
