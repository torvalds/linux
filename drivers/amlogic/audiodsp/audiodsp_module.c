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
#include <mach/am_regs.h>
#include <linux/amlogic/major.h>
#include <linux/slab.h>

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6  
#include <mach/mod_gate.h>
#include <mach/power_gate.h>
#endif

//#include <asm/dsp/audiodsp_control.h>
#include "audiodsp_control.h"	// temp here

#include <asm/uaccess.h>
#include <linux/amlogic/amports/amstream.h>

#include "audiodsp_module.h"
#include "dsp_control.h"
#include "dsp_microcode.h"
#include "dsp_mailbox.h"
#include "dsp_monitor.h"

#include "dsp_codec.h"
#include <linux/dma-mapping.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/timestamp.h>
#include <linux/amlogic/amports/tsync.h>

extern void tsync_pcr_recover(void);extern void tsync_pcr_recover(void);

MODULE_DESCRIPTION("AMLOGIC APOLLO Audio dsp driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhou Zhi <zhi.zhou@amlogic.com>");
MODULE_VERSION("1.0.0");

extern unsigned IEC958_mode_raw;
extern unsigned IEC958_mode_codec;
extern int decopt;
static int IEC958_mode_raw_last = 0;
static int IEC958_mode_codec_last = 0;
/* code for DD/DD+ DRC control  */
/* Dynamic range compression mode */
typedef enum {
    GBL_COMP_CUSTOM_0 = 0, /* custom mode, analog  dialnorm */
    GBL_COMP_CUSTOM_1,     /* custom mode, digital dialnorm */
    GBL_COMP_LINE,         /* line out mode (default)       */
    GBL_COMP_RF            /* RF mode                       */
} DDP_DEC_DRC_MODE;
#define DRC_MODE_BIT  0
#define DRC_HIGH_CUT_BIT 3
#define DRC_LOW_BST_BIT 16
static unsigned ac3_drc_control = (GBL_COMP_LINE<<DRC_MODE_BIT)|(100<<DRC_HIGH_CUT_BIT)|(100<<DRC_LOW_BST_BIT);
/* code for DTS dial norm/downmix mode control */
typedef enum{
	DTS_DMX_LoRo = 0,
	DTS_DMX_LtRt,
}DTS_DMX_MODE;
#define DTS_DMX_MODE_BIT  0
#define DTS_DIAL_NORM_BIT  1
#define DTS_DRC_SCALE_BIT  2
static unsigned dts_dec_control = (DTS_DMX_LoRo<<DTS_DMX_MODE_BIT	)|(1<<DTS_DIAL_NORM_BIT)|(0<<DTS_DRC_SCALE_BIT);
unsigned dsp_debug_flag;

extern struct audio_info * get_audio_info(void);
extern void	aml_alsa_hw_reprepare(void);
extern void dsp_get_debug_interface(int flag);
void audiodsp_moniter(unsigned long);
static struct audiodsp_priv *audiodsp_p;
#define  DSP_DRIVER_NAME	"audiodsp"
#define  DSP_NAME	"dsp"

/**
 *  Audio codec necessary MIPS (KHz)
 */
 #if 0
static unsigned int audiodsp_mips[]={
    200000, //#define MCODEC_FMT_MPEG123 (1<<0)
    200000, //#define MCODEC_FMT_AAC 	  (1<<1)
    200000, //#define MCODEC_FMT_AC3 	  (1<<2)
    200000, //#define MCODEC_FMT_DTS		  (1<<3)
    200000, //#define MCODEC_FMT_FLAC	  (1<<4)
    200000, //#define MCODEC_FMT_COOK		(1<<5)
    200000, //#define MCODEC_FMT_AMR		(1<<6)
    200000, //#define MCODEC_FMT_RAAC     (1<<7)
    200000, //#define MCODEC_FMT_ADPCM	  (1<<8)
    200000, //#define MCODEC_FMT_WMA     (1<<9)
    200000, //#define MCODEC_FMT_PCM      (1<<10)
    200000, //#define MCODEC_FMT_WMAPRO      (1<<11)
};
#endif
#ifdef CONFIG_PM
typedef struct {
    int event;
    //
}audiodsp_pm_state_t;

static audiodsp_pm_state_t pm_state;

#endif

static void audiodsp_prevent_sleep(void)
{
    /*struct audiodsp_priv* priv = */audiodsp_privdata();
    printk("audiodsp prevent sleep\n");
    //wake_lock(&priv->wakelock);
}

static void audiodsp_allow_sleep(void)
{
    /*struct audiodsp_priv *priv=*/audiodsp_privdata();
    printk("audiodsp allow sleep\n");
    //wake_unlock(&priv->wakelock);
}

int audiodsp_start(void)
{
	struct audiodsp_priv *priv=audiodsp_privdata();
	struct audiodsp_microcode *pmcode;
	struct audio_info *audio_info;
	int ret,i;
	priv->frame_format.valid=0;
	priv->decode_error_count=0;
	priv->last_valid_pts=0;
	priv->out_len_after_last_valid_pts = 0;
	priv->decode_fatal_err = 0;
	priv->first_lookup_over = 0;
	pmcode=audiodsp_find_supoort_mcode(priv,priv->stream_fmt);
	if(pmcode==NULL)
	{
		DSP_PRNT("have not find a valid mcode for fmt(0x%x)\n",priv->stream_fmt);
		return -1;
	}

	stop_audiodsp_monitor(priv);
	dsp_stop(priv);
	ret=dsp_start(priv,pmcode);
	if(ret==0){
#ifndef CONFIG_MESON_TRUSTZONE    
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
		if(priv->stream_fmt == MCODEC_FMT_DTS || priv->stream_fmt == MCODEC_FMT_AC3 || priv->stream_fmt == MCODEC_FMT_EAC3){
			dsp_get_debug_interface(1);
		}
#endif
#endif		
 		start_audiodsp_monitor(priv);

#ifdef CONFIG_AM_VDEC_REAL
	if((pmcode->fmt == MCODEC_FMT_COOK) || 
	   (pmcode->fmt == MCODEC_FMT_RAAC) || 
	   (pmcode->fmt == MCODEC_FMT_AMR)  || 
	   (pmcode->fmt == MCODEC_FMT_WMA)  ||
	   (pmcode->fmt == MCODEC_FMT_ADPCM)|| 
	   (pmcode->fmt == MCODEC_FMT_PCM)  ||
	   (pmcode->fmt == MCODEC_FMT_WMAPRO)||
	   (pmcode->fmt == MCODEC_FMT_ALAC)||
	  (pmcode->fmt & MCODEC_FMT_AC3) ||
	   (pmcode->fmt & MCODEC_FMT_EAC3) ||	  
	  (pmcode->fmt == MCODEC_FMT_APE) ||
	  (pmcode->fmt == MCODEC_FMT_FLAC))

	{
		DSP_PRNT("dsp send audio info\n");
    		for(i = 0; i< 2000;i++){
                if(DSP_RD(DSP_AUDIOINFO_STATUS) == DSP_AUDIOINFO_READY)//maybe at audiodsp side,INT not enabled yet,so wait a while
                    break;
    		     msleep(1);
            }
		if(i == 2000)
			DSP_PRNT("audiodsp not ready for info  \n");
            DSP_WD(DSP_AUDIOINFO_STATUS,0);
		    audio_info = get_audio_info();
		DSP_PRNT("kernel sent info first 4 byte[0x%x],[0x%x],[0x%x],[0x%x]\n\t",audio_info->extradata[0],\
			audio_info->extradata[1],audio_info->extradata[2],audio_info->extradata[3]);
		DSP_WD(DSP_GET_EXTRA_INFO_FINISH, 0);
		while(1){
		    dsp_mailbox_send(priv, 1, M2B_IRQ4_AUDIO_INFO, 0, (const char*)audio_info, sizeof(struct audio_info));
		    msleep(100);

		    if(DSP_RD(DSP_GET_EXTRA_INFO_FINISH) == 0x12345678)
		        break;
		}
    }
#endif
     }
	return ret;
}

static int audiodsp_open(struct inode *node, struct file *file)
{
	DSP_PRNT("dsp_open\n");

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	CLK_GATE_ON(AHB_BRIDGE);
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6	
	switch_mod_gate_by_type(MOD_MEDIA_CPU, 1);
        /* Audio DSP firmware uses mailbox registers for communications 
	 * with host processor. And these mailbox registers unfortunately 
	 * falls into the assistant module, which is in vdec DOS clock domain.
         * So we have to make sure the clock domain is enabled/disabled when
         * audio firmware start/stop running.
         * Note the module_gating has ref count so a flag 0 does
         * not mean the vdec clock is gated off immediately.
         */
	switch_mod_gate_by_type(MOD_VDEC, 1);
#endif
	audiodsp_prevent_sleep();
	return 0;

}

static unsigned long audiodsp_drop_pcm(unsigned long size)
{
	struct audiodsp_priv *priv = audiodsp_privdata();
	size_t len;
	int count = 0;
	unsigned long drop_bytes = size;

	mutex_lock(&priv->stream_buffer_mutex);
	if(priv->stream_buffer_mem == NULL || !priv->dsp_is_started)
		goto err;
	
	while(drop_bytes > 0){
		len =dsp_codec_get_bufer_data_len(priv);
		if(drop_bytes >= len){
			dsp_codec_inc_rd_addr(priv, len);
			drop_bytes -= len;
			msleep(50);
			count++;
			if(count > 20)
			    break;
		}
		else {
			dsp_codec_inc_rd_addr(priv, drop_bytes);
			drop_bytes = 0;
		}	
	}

	mutex_unlock(&priv->stream_buffer_mutex);
	if(count > 10)
		printk("drop pcm data timeout! count = %d\n", count);
	
	return (size - drop_bytes);
	
err:
		mutex_unlock(&priv->stream_buffer_mutex);
		printk("error, can not drop pcm data!\n");
		return 0;
}

static long audiodsp_ioctl(struct file *file, unsigned int cmd,
		      unsigned long args)
{
	struct audiodsp_priv *priv=audiodsp_privdata();
	struct audiodsp_cmd *a_cmd;
	char name[64];
	int len;
	unsigned long pts;
	int ret=0;
	unsigned long drop_size;
	static int wait_format_times=0;
	switch(cmd)
		{
		case AUDIODSP_SET_FMT:						
			priv->stream_fmt=args;
			if(IEC958_mode_raw){// raw data pass through		
				if(args == MCODEC_FMT_DTS)
					IEC958_mode_codec = ((decopt>>5)&1)?3:1;//dts PCM/RAW mode
				else if(args == MCODEC_FMT_AC3)
					IEC958_mode_codec = 2; 	//ac3
				else if(args == MCODEC_FMT_EAC3){
					if(IEC958_mode_raw == 2)
						IEC958_mode_codec = 4; //958 dd+ package
					else
						IEC958_mode_codec =2;// 958 dd package
				}	
				else
					IEC958_mode_codec = 0;
			}	
			else
				IEC958_mode_codec = 0;
			if(args == MCODEC_FMT_AC3||args == MCODEC_FMT_EAC3) //for dd+ certification
				DSP_WD(DSP_AC3_DRC_INFO,ac3_drc_control|(1<<31));
			else if(args == MCODEC_FMT_DTS)
				DSP_WD(DSP_DTS_DEC_INFO,dts_dec_control|(1<<31));
			break;
		case AUDIODSP_START:						
			if(IEC958_mode_codec || (IEC958_mode_codec_last != IEC958_mode_codec))
			{
				IEC958_mode_raw_last = IEC958_mode_raw;
				IEC958_mode_codec_last = IEC958_mode_codec;
				aml_alsa_hw_reprepare();
			}	
			priv->decoded_nb_frames = 0;
			priv->format_wait_count = 0;
			if(priv->stream_fmt<=0)
				{
				DSP_PRNT("Audio dsp steam format have not set!\n");
				}
			else
				{
#ifndef CONFIG_MESON_TRUSTZONE    
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
                               if(priv->stream_fmt == MCODEC_FMT_DTS || priv->stream_fmt == MCODEC_FMT_AC3 || priv->stream_fmt == MCODEC_FMT_EAC3)
                                       dsp_get_debug_interface(0);    
#endif
#endif					
				ret=audiodsp_start();
				}
			break;
		case AUDIODSP_STOP:
			/* restore aiu958 setting to pcm */
			if(IEC958_mode_codec_last)
				aml_alsa_hw_reprepare();
			IEC958_mode_codec = 0;
			//DSP_PRNT("audiodsp command stop\n");
			stop_audiodsp_monitor(priv);
			dsp_stop(priv);
			priv->decoded_nb_frames = 0;
			priv->format_wait_count = 0;
			break;
		case AUDIODSP_DECODE_START:			
			if(priv->dsp_is_started)
				{
				dsp_codec_start(priv);
				wait_format_times=0;
				}
			else
				{
				DSP_PRNT("Audio dsp have not started\n");
				}			
			break;
		case AUDIODSP_WAIT_FORMAT:
			if(priv->dsp_is_started)
			{
				ret = audiodsp_get_audioinfo(priv);
			}
			else
			{
				ret = -1;
				DSP_PRNT("Audio dsp have not started\n");
			}
			/*Reset the PLL. Added by GK*/
			tsync_pcr_recover();			
			break;
		case AUDIODSP_DECODE_STOP:
			if(priv->dsp_is_started)
				{
				dsp_codec_stop(priv);
				}
			else
				{
				DSP_PRNT("Audio dsp have not started\n");
				}
			
			break;
		case AUDIODSP_REGISTER_FIRMWARE:
			a_cmd=(struct audiodsp_cmd *)args;
		//	DSP_PRNT("register firware,%d,%s\n",a_cmd->fmt,a_cmd->data);
			len=a_cmd->data_len>64?64:a_cmd->data_len;
			if (copy_from_user(name,a_cmd->data,len)) {
				return -EFAULT;
			}
			name[len]='\0';
			ret=audiodsp_microcode_register(priv,
								a_cmd->fmt,
								name);
			break;
		case AUDIODSP_UNREGISTER_ALLFIRMWARE:
			  audiodsp_microcode_free(priv);
			break;
		case AUDIODSP_GET_CHANNELS_NUM: 
			put_user(-1,(__s32 __user *)args);/*mask data is not valid*/
			if(priv->frame_format.valid & CHANNEL_VALID)
				{ 
				put_user(priv->frame_format.channel_num,(__s32 __user *)args);
				}
			break;
		case AUDIODSP_GET_SAMPLERATE: 
			put_user(-1,(__s32 __user *)args);/*mask data is not valid*/
			if(priv->frame_format.valid & SAMPLE_RATE_VALID)
				{ 
				put_user(priv->frame_format.sample_rate,(__s32 __user *)args);
				} 
			break;
		case AUDIODSP_GET_DECODED_NB_FRAMES: 			
				put_user(priv->decoded_nb_frames,(__s32 __user *)args);
			break;
		case AUDIODSP_GET_BITS_PER_SAMPLE: 
			put_user(-1,(__s32 __user *)args);/*mask data is not valid*/
			if(priv->frame_format.valid & DATA_WIDTH_VALID)
				{
				put_user(priv->frame_format.data_width,(__s32 __user *)args);
				} 
			break;
		case AUDIODSP_GET_PTS:
			/*val=-1 is not valid*/
			put_user(dsp_codec_get_current_pts(priv),(__u32 __user *)args);
			break;
		case AUDIODSP_LOOKUP_APTS:
			{
				u32 pts, offset;
				get_user(offset,(__u32 __user *)args);
				pts_lookup_offset(PTS_TYPE_AUDIO, offset, &pts, 300);
				put_user(pts,(__u32 __user *)args);
			}
			break;
		case AUDIODSP_GET_FIRST_PTS_FLAG:
			if(priv->stream_fmt == MCODEC_FMT_COOK || priv->stream_fmt == MCODEC_FMT_RAAC)
				put_user(1,(__s32 __user *)args);
			else
				put_user(first_pts_checkin_complete(PTS_TYPE_AUDIO),(__s32 __user *)args);
			break;
			
		case AUDIODSP_SYNC_AUDIO_START:

			if(get_user(pts, (unsigned long __user *)args)) {
				printk("Get start pts from user space fault! \n");
				return -EFAULT;
			}
			tsync_avevent(AUDIO_START, pts);
			
			break;
			
		case AUDIODSP_SYNC_AUDIO_PAUSE:

			tsync_avevent(AUDIO_PAUSE, 0);
			break;

		case AUDIODSP_SYNC_AUDIO_RESUME:

			tsync_avevent(AUDIO_RESUME, 0);
			break;

		case AUDIODSP_SYNC_AUDIO_TSTAMP_DISCONTINUITY:

			if(get_user(pts, (unsigned long __user *)args)){
				printk("Get audio discontinuity pts fault! \n");
				return -EFAULT;
			}
			tsync_avevent(AUDIO_TSTAMP_DISCONTINUITY, pts);
			
			break;

		case AUDIODSP_SYNC_GET_APTS:

			pts = timestamp_apts_get();
			
			if(put_user(pts, (unsigned long __user *)args)){
				printk("Put audio pts to user space fault! \n");
				return -EFAULT;
			}
			
			break;

		case AUDIODSP_SYNC_GET_PCRSCR:

			pts = timestamp_pcrscr_get();

			if(put_user(pts, (unsigned long __user *)args)){
				printk("Put pcrscr to user space fault! \n");
				return -EFAULT;
			}
			
			break;

		case AUDIODSP_SYNC_SET_APTS:

			if(get_user(pts, (unsigned long __user *)args)){
				printk("Get audio pts from user space fault! \n");
				return -EFAULT;
			}
			tsync_set_apts(pts);
			
			break;

		case AUDIODSP_DROP_PCMDATA:

			if(get_user(drop_size, (unsigned long __user *)args)){
				printk("Get pcm drop size from user space fault! \n");
				return -EFAULT;
			}
			audiodsp_drop_pcm(drop_size);
		break;
			
    case AUDIODSP_AUTOMUTE_ON:
         tsync_set_automute_on(1);
    break;

    case AUDIODSP_AUTOMUTE_OFF:
         tsync_set_automute_on(0);
		break;

    case AUDIODSP_GET_PCM_LEVEL:
        {
            int len = dsp_codec_get_bufer_data_len(priv);
            if(put_user(len, (unsigned long __user *)args)){
    			printk("Put pcm level to user space fault! \n");
    			return -EFAULT;
    		}
            break;
        }
        
    case AUDIODSP_SET_PCM_BUF_SIZE:
        if ((int)args > 0) {
            priv->stream_buffer_mem_size = args;
        }
        break;
    case AUDIODSP_SKIP_BYTES:
        DSP_WD(DSP_SKIP_BYTES, (unsigned int)args);
        break;    
		default:
			DSP_PRNT("unsupport cmd number%d\n",cmd);
			ret=-1;
		}
	return ret;
}

static int audiodsp_release(struct inode *node, struct file *file)
{
	DSP_PRNT("dsp_release\n");
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6	
	switch_mod_gate_by_type(MOD_VDEC, 0);
	switch_mod_gate_by_type(MOD_MEDIA_CPU, 0);
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        CLK_GATE_OFF(AHB_BRIDGE);
#endif

	audiodsp_allow_sleep();
	return 0;
}

ssize_t audiodsp_read(struct file * file, char __user * ubuf, size_t size,
		  loff_t * loff)
{
	struct audiodsp_priv *priv=audiodsp_privdata();
	unsigned long rp,orp;
	size_t len;
	size_t else_len;
	size_t wlen;
	size_t w_else_len;
	int wait=0;
	 char __user *pubuf=ubuf;
     dma_addr_t buf_map;



	#define MIN_READ	2	// 1 channel * 2 bytes per sample
	#define PCM_DATA_MIN	2
	#define PCM_DATA_ALGIN(x) (x & (~(PCM_DATA_MIN-1)))
	#define MAX_WAIT		HZ/1000
	
	mutex_lock(&priv->stream_buffer_mutex);	
	if(priv->stream_buffer_mem==NULL || !priv->dsp_is_started)
		goto error_out;
	
	do{
			len=dsp_codec_get_bufer_data_len(priv);
			if(len>MIN_READ)
				break;
			else
				{
				if(wait>0)
					break;
				wait++;
				init_completion(&priv->decode_completion);
				wait_for_completion_timeout(&priv->decode_completion, MAX_WAIT);
				}
		}while(len<MIN_READ);
	if(len>priv->stream_buffer_size || len <0)
		{
		DSP_PRNT("audio stream buffer is bad len=%d\n",len);
		goto error_out;
		}
	len=min(len,size);
	len=PCM_DATA_ALGIN(len);
	else_len=len;
	rp=dsp_codec_get_rd_addr(priv);
	orp=rp;
	while(else_len>0)
		{
		
		wlen=priv->stream_buffer_end-rp;
		wlen=min(wlen,else_len);
///		dma_cache_inv((unsigned long)rp,wlen);    
        buf_map = dma_map_single(NULL, (void *)rp, wlen, DMA_FROM_DEVICE);
		w_else_len=copy_to_user((void*)pubuf,(const char *)(rp),wlen);
		if(w_else_len!=0)
			{
			DSP_PRNT("copyed error,%d,%d,[%p]<---[%lx]\n",w_else_len,wlen,pubuf,rp);
			wlen-=w_else_len;
			}      
        dma_unmap_single(NULL, buf_map, wlen, DMA_FROM_DEVICE);
		else_len-=wlen;
		pubuf+=wlen;
		rp=dsp_codec_inc_rd_addr(priv,wlen);
		}
	priv->out_len_after_last_valid_pts+=len;
	mutex_unlock(&priv->stream_buffer_mutex);
	//u32 timestamp_pcrscr_get(void);
	//printk("current pts=%ld,src=%ld\n",dsp_codec_get_current_pts(priv),timestamp_pcrscr_get());
	return len;
error_out:
	mutex_unlock(&priv->stream_buffer_mutex);
    printk("audiodsp_read failed\n");
	return -EINVAL;
}

ssize_t audiodsp_write(struct file * file, const char __user * ubuf, size_t size,
		   loff_t * loff)
{
	struct audiodsp_priv *priv=audiodsp_privdata();
	// int dsp_codec_start( struct audiodsp_priv *priv);
	// int dsp_codec_stop( struct audiodsp_priv *priv);
	audiodsp_microcode_register(priv,
								MCODEC_FMT_COOK,
								"audiodsp_codec_cook.bin");
	priv->stream_fmt=MCODEC_FMT_COOK;
	audiodsp_start();
	dsp_codec_start(priv);
	//dsp_codec_stop(priv);
	
	return size;
}

const static struct file_operations audiodsp_fops = {
	.owner = THIS_MODULE,
	.open =audiodsp_open,
	.read = audiodsp_read,
	.write = audiodsp_write,
	.release = audiodsp_release,
	.unlocked_ioctl = audiodsp_ioctl,
};
static int audiodsp_get_status(struct adec_status *astatus)
{
	struct audiodsp_priv *priv=audiodsp_privdata();
	if(!astatus)
		return -EINVAL;
	if(priv->frame_format.valid & CHANNEL_VALID)
		astatus->channels=priv->frame_format.channel_num;
	else
		astatus->channels=0;
	if(priv->frame_format.valid & SAMPLE_RATE_VALID)
		astatus->sample_rate=priv->frame_format.sample_rate;
	else
		astatus->sample_rate=0;
	if(priv->frame_format.valid & DATA_WIDTH_VALID)
		astatus->resolution=priv->frame_format.data_width;
	else
		astatus->resolution=0;
	astatus->error_count=priv->decode_error_count;
	astatus->status=priv->dsp_is_started?0:1;
	return 0;
}
static int audiodsp_init_mcode(struct audiodsp_priv *priv)
{
	spin_lock_init(&priv->mcode_lock);
	priv->mcode_id=0;
	priv->dsp_stack_start=0;
	priv->dsp_gstack_start=0;
	priv->dsp_heap_start=0;
	priv->code_mem_size=AUDIO_DSP_MEM_SIZE -REG_MEM_SIZE;
	priv->dsp_code_start=AUDIO_DSP_START_ADDR;
    DSP_PRNT("DSP start addr 0x%x\n",AUDIO_DSP_START_ADDR);
	priv->dsp_stack_size=1024*64;
	priv->dsp_gstack_size=512;
	priv->dsp_heap_size=1024*1024;
	priv->stream_buffer_mem=NULL;
	priv->stream_buffer_mem_size=32*1024;
	priv->stream_fmt=-1;
	INIT_LIST_HEAD(&priv->mcode_list);
	init_completion(&priv->decode_completion);
	mutex_init(&priv->stream_buffer_mutex);
	mutex_init(&priv->dsp_mutex);
	priv->last_stream_fmt=-1;
	priv->last_valid_pts=0;
	priv->out_len_after_last_valid_pts=0;
	priv->dsp_work_details = (struct dsp_working_info*)DSP_WORK_INFO;
	return 0;
}

static ssize_t codec_fmt_show(struct class* cla, struct class_attribute* attr, char* buf)
{
    size_t ret = 0;
    struct audiodsp_priv *priv = audiodsp_privdata();
    ret = sprintf(buf, "The codec Format %d\n", priv->stream_fmt);
    return ret;
}
#if 0
static ssize_t codec_mips_show(struct class* cla, struct class_attribute* attr, char* buf)
{
    size_t ret = 0;
    struct audiodsp_priv *priv = audiodsp_privdata();
    if(priv->stream_fmt < sizeof(audiodsp_mips)){    
        ret = sprintf(buf, "%d\n", audiodsp_mips[__builtin_ffs(priv->stream_fmt)]);
    }
    else{
        ret = sprintf(buf, "%d\n", 200000);
    }
    return ret;
}
#endif
static ssize_t codec_fatal_err_show(struct class* cla, struct class_attribute* attr, char* buf)
{
    struct audiodsp_priv *priv = audiodsp_privdata();
	
    return sprintf(buf, "%d\n", priv->decode_fatal_err);
}
static ssize_t codec_fatal_err_store(struct class* cla, struct class_attribute* attr, const char* buf,
	                                       size_t count)
{
     struct audiodsp_priv *priv = audiodsp_privdata();
	
	if(buf[0] == '0'){
	  priv->decode_fatal_err = 0;	  
	}else if(buf[0] == '1'){
	  priv->decode_fatal_err = 1;	  
	}else if(buf[0] == '2'){
      priv->decode_fatal_err = 2;
	}
	printk("codec_fatal_err value:%d \n ",priv->decode_fatal_err);
	return count;
}
static ssize_t swap_buf_ptr_show(struct class *cla, struct class_attribute* attr, char* buf)
{
    char *pbuf = buf;

    pbuf += sprintf(pbuf, "swap buffer wp: %lx\n", DSP_RD(DSP_DECODE_OUT_WD_PTR));
    pbuf += sprintf(pbuf, "swap buffer rp: %lx\n", DSP_RD(DSP_DECODE_OUT_RD_ADDR));

    return (pbuf - buf);
}
 
static ssize_t dsp_working_status_show(struct class* cla, struct class_attribute* attr, char* buf)
{
    struct audiodsp_priv *priv = audiodsp_privdata();
    struct dsp_working_info *info = priv->dsp_work_details;
    char *pbuf = 	buf;
    pbuf += sprintf(pbuf, "\tdsp status  0x%lx\n", DSP_RD(DSP_STATUS));
    pbuf += sprintf(pbuf, "\tdsp sp  0x%x\n", info->sp);
 //   pbuf += sprintf(pbuf, "\tdsp pc  0x%x\n", info->pc);
    pbuf += sprintf(pbuf, "\tdsp ilink1  0x%x\n", info->ilink1);
    pbuf += sprintf(pbuf, "\tdsp ilink2  0x%x\n", info->ilink2);
    pbuf += sprintf(pbuf, "\tdsp blink  0x%x\n", info->blink);
    pbuf += sprintf(pbuf, "\tdsp jeffies  0x%lx\n", DSP_RD(DSP_JIFFIES));
    pbuf += sprintf(pbuf, "\tdsp pcm wp  0x%lx\n", DSP_RD(DSP_DECODE_OUT_WD_ADDR));
    pbuf += sprintf(pbuf, "\tdsp pcm rp  0x%lx\n", DSP_RD(DSP_DECODE_OUT_RD_ADDR));
    pbuf += sprintf(pbuf, "\tdsp pcm buffer level  0x%x\n", dsp_codec_get_bufer_data_len(priv));
    pbuf += sprintf(pbuf, "\tdsp pcm buffered size  0x%lx\n", DSP_RD(DSP_BUFFERED_LEN));
    pbuf += sprintf(pbuf, "\tdsp es read offset  0x%lx\n", DSP_RD(DSP_AFIFO_RD_OFFSET1));

    return 	(pbuf- buf);
}

static ssize_t digital_raw_show(struct class*cla, struct class_attribute* attr, char* buf)
{
  static char* digital_format[] = {
    "0 - PCM",
    "1 - RAW w/o over clock",
    "2 - RAW w/  over clock",
  };
  char* pbuf = buf;
  printk("Digital output mode: %s\n", digital_format[IEC958_mode_raw]);
  pbuf += sprintf(pbuf, "%d\n",IEC958_mode_raw);
  return (pbuf-buf);
}
static ssize_t digital_raw_store(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  printk("buf=%s\n", buf);
  if(buf[0] == '0'){
    IEC958_mode_raw = 0;	// PCM
  }else if(buf[0] == '1'){
    IEC958_mode_raw = 1;	// RAW without over clock
  }else if(buf[0] == '2'){
    IEC958_mode_raw = 2;	// RAW with over clock
  }
  printk("IEC958_mode_raw=%d\n", IEC958_mode_raw);
  return count;
}
#define SUPPORT_TYPE_NUM  8
static unsigned char *codec_str[SUPPORT_TYPE_NUM] = {"2 CH PCM","DTS RAW Mode","Dolby Digital","DTS","DD+","DTSHD","8 CH PCM","TrueHD"};
static ssize_t digital_codec_show(struct class*cla, struct class_attribute* attr, char* buf)
{
  char* pbuf = buf;

  //printk("IEC958_mode_codec/%d Digital codec type: %s\n",IEC958_mode_codec, codec_format[IEC958_mode_codec]);
  pbuf += sprintf(pbuf, "%d\n",IEC958_mode_codec);
  return (pbuf-buf);
}
static ssize_t digital_codec_store(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
	int digital_codec  = 0;
	if(buf){
  		digital_codec = simple_strtoul(buf, NULL, 10);
		if(digital_codec < SUPPORT_TYPE_NUM){
			IEC958_mode_codec = digital_codec;
			printk("IEC958_mode_codec= %d,IEC958 type %s \n",digital_codec,codec_str[digital_codec]);
		}
		else{
			printk("IEC958 type set exceed supported range \n");
		}
	}		
  return count;
}
static ssize_t print_flag_show(struct class*cla, struct class_attribute* attr, char* buf)
{
  static char* dec_format[] = {
    "0 - disable arc dsp print",
    "1 - enable arc dsp print",  };
  char* pbuf = buf;
  pbuf += sprintf(pbuf, "audiodsp decode option: %s\n", dec_format[(decopt&0x5)>>2]);
  return (pbuf-buf);
}
static ssize_t print_flag_store(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  unsigned dec_opt = 0x1;
  printk("buf=%s\n", buf);
  if(buf[0] == '0'){
    dec_opt = 0;	// disable print flag
  }else if(buf[0] == '1'){
    dec_opt = 1;	// enable print flag
  }
  
  decopt = 	(decopt&(~4))|(dec_opt<<2);
  printk("dec option=%d, decopt = %x\n", dec_opt, decopt);
  return count;
}

static ssize_t dec_option_show(struct class*cla, struct class_attribute* attr, char* buf)
{
  static char* dec_format[] = {
	"0 - mute dts and ac3 ",  	
    "1 - mute dts.ac3 with noise ",
    "2 - mute ac3.dts with noise",
    "3 - both ac3 and dts with noise",
  };
  char* pbuf = buf;
  pbuf += sprintf(pbuf, "audiodsp decode option: %s\n", dec_format[decopt&0x3]);
  return (pbuf-buf);
}
static ssize_t dec_option_store(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  unsigned dec_opt = 0x3;
  printk("buf=%s\n", buf);
  if(buf[0] == '0'){
    dec_opt = 0;	// mute ac3/dts
  }else if(buf[0] == '1'){
    dec_opt = 1;	// mute dts
  }else if(buf[0] == '2'){
    dec_opt = 2;	// mute ac3
  }else if(buf[0] == '3'){
  	dec_opt = 3; //with noise
  }else if(buf[0] == '4'){
  	  printk("digital mode :PCM-raw  \n"); 
	  decopt |= (1<<5); 
  }else if(buf[0] == '5'){
	  printk("digital mode :raw \n"); 
	  decopt &= ~(1<<5);
  }
  decopt = 	(decopt&(~3))|dec_opt;
  printk("dec option=%d\n", dec_opt);
  return count;
}
static ssize_t ac3_drc_control_show(struct class*cla, struct class_attribute* attr, char* buf)
{
	char *drcmode[] = {"CUSTOM_0","CUSTOM_1","LINE","RF"};
	char *pbuf = buf;
#if 0	
	pbuf += sprintf(pbuf, "\tdd+ drc mode : %s\n", drcmode[ac3_drc_control&0x3]);
	pbuf += sprintf(pbuf, "\tdd+ drc high cut scale : %d%%\n", (ac3_drc_control>>DRC_HIGH_CUT_BIT)&0xff);
	pbuf += sprintf(pbuf, "\tdd+ drc low boost scale : %d%%\n", (ac3_drc_control>>DRC_LOW_BST_BIT)&0xff);
#else
	printk( "\tdd+ drc mode : %s\n", drcmode[ac3_drc_control&0x3]);
	printk( "\tdd+ drc high cut scale : %d%%\n", (ac3_drc_control>>DRC_HIGH_CUT_BIT)&0xff);
	printk("\tdd+ drc low boost scale : %d%%\n", (ac3_drc_control>>DRC_LOW_BST_BIT)&0xff);
  	pbuf += sprintf(pbuf, "%d\n",ac3_drc_control);
#endif
	return (pbuf-buf);
}
static ssize_t ac3_drc_control_store(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
    char tmpbuf[128];
	char *drcmode[] = {"CUSTOM_0","CUSTOM_1","LINE","RF"};	
    int i=0;
	unsigned val;
    while((buf[i])&&(buf[i]!=',')&&(buf[i]!=' ')){
        tmpbuf[i]=buf[i];
        i++;    
    }
    tmpbuf[i]=0;
 	if(strncmp(tmpbuf, "drcmode", 7)==0){
        val=simple_strtoul(buf+i+1, NULL, 16); 
		val = val&0x3;
		printk("drc mode set to %s\n",drcmode[val]);
		ac3_drc_control = (ac3_drc_control&(~3))|val;
 	}
 	else if(strncmp(tmpbuf, "drchighcutscale", 15)==0){
        val=simple_strtoul(buf+i+1, NULL, 16); 	
		val = val&0xff;
		printk("drc high cut scale set to %d%%\n",val);
		ac3_drc_control = (ac3_drc_control&(~(0xff<<DRC_HIGH_CUT_BIT)))|(val<<DRC_HIGH_CUT_BIT);
 	}
 	else if(strncmp(tmpbuf, "drclowboostscale", 16)==0){
        val=simple_strtoul(buf+i+1, NULL, 16); 	
		val = val&0xff;
		printk("drc low boost scale set to %d%%\n",val);
		ac3_drc_control = (ac3_drc_control&(~(0xff<<DRC_LOW_BST_BIT)))|(val<<DRC_LOW_BST_BIT);
 	}
	else
		printk("invalid args\n");
	return count;

}
static ssize_t dts_dec_control_show(struct class*cla, struct class_attribute* attr, char* buf)
{
	//char *dmxmode[] = {"Lo/Ro","Lt/Rt"};
	//char *dialnorm[] = {"disable","enable"};	
	char *pbuf = buf;
	pbuf += sprintf(pbuf, "%d\n",dts_dec_control);
	return (pbuf-buf);
}
static ssize_t dts_dec_control_store(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
    char tmpbuf[128];
	char *dmxmode[] = {"Lo/Ro","Lt/Rt"};
	char *dialnorm[] = {"disable","enable"};		
    int i=0;
	unsigned val;
    while((buf[i])&&(buf[i]!=',')&&(buf[i]!=' ')){
        tmpbuf[i]=buf[i];
        i++;    
    }
    tmpbuf[i]=0;
 	if(strncmp(tmpbuf, "dtsdmxmode", 10)==0){
        val=simple_strtoul(buf+i+1, NULL, 16); 
		val = val&0x1;
		printk("dts dmx mode set to %s\n",dmxmode[val]);
		dts_dec_control = (dts_dec_control&(~1))|val;
 	}
 	else if(strncmp(tmpbuf, "dtsdrcscale", 11)==0){
        val=simple_strtoul(buf+i+1, NULL, 16); 	
		val = val&0xff;
		printk("dts drc  scale set to %d\n",val);
		dts_dec_control = (dts_dec_control&(~(0xff<<DTS_DRC_SCALE_BIT)))|(val<<DTS_DRC_SCALE_BIT);
 	}
 	else if(strncmp(tmpbuf, "dtsdialnorm", 11)==0){
        val=simple_strtoul(buf+i+1, NULL, 16); 	
		val = val&0x1;
		printk("dts  dial norm : set to %s\n",dialnorm[val]);
		dts_dec_control = (dts_dec_control&(~(0x1<<DTS_DIAL_NORM_BIT)))|(val<<DTS_DIAL_NORM_BIT);
 	}
	else{
		dts_dec_control=simple_strtoul(tmpbuf, NULL, 10);
		printk("dts_dec_control/0x%x\n",dts_dec_control);
	}
	return count;

}

static ssize_t dsp_debug_show(struct class*cla, struct class_attribute* attr, char* buf)
{
  char* pbuf = buf;
  pbuf += sprintf(pbuf, "DSP Debug Flag: %s\n", (dsp_debug_flag==0)?"0 - OFF":"1 - ON");
  return (pbuf-buf);
}
static ssize_t dsp_debug_store(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  if(buf[0] == '0'){
    dsp_debug_flag = 0;
  }else if(buf[0] == '1'){
    dsp_debug_flag = 1;
  }
  printk("DSP Debug Flag: %d\n", dsp_debug_flag);
  return count;
}

static ssize_t skip_rawbytes_show(struct class*cla, struct class_attribute* attr, char* buf)
{
  unsigned long bytes = DSP_RD(DSP_SKIP_BYTES);
  return sprintf(buf, "%ld\n", bytes);
}

static ssize_t skip_rawbytes_store(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
  unsigned long bytes = simple_strtoul(buf, NULL, 16);
  DSP_WD(DSP_SKIP_BYTES, bytes);
  printk("audio stream SKIP when ablevel>0x%lx\n", bytes);
  return count;
}

static ssize_t pcm_left_len_show(struct class*cla, struct class_attribute* attr, char* buf)
{

	struct audiodsp_priv *priv = audiodsp_privdata();
  int len = dsp_codec_get_bufer_data_len(priv);
  return sprintf(buf, "%d\n", len);
}
static struct class_attribute audiodsp_attrs[]={
    __ATTR_RO(codec_fmt),
#ifdef CONFIG_ARCH_MESON1    
    __ATTR_RO(codec_mips),
#endif    
    __ATTR(codec_fatal_err, S_IRUGO | S_IWUSR | S_IWGRP, codec_fatal_err_show, codec_fatal_err_store),
    __ATTR_RO(swap_buf_ptr),
    __ATTR_RO(dsp_working_status),
    __ATTR(digital_raw, S_IRUGO | S_IWUSR | S_IWGRP, digital_raw_show, digital_raw_store),
     __ATTR(digital_codec, S_IRUGO | S_IWUSR | S_IWGRP, digital_codec_show, digital_codec_store),
    __ATTR(dec_option, S_IRUGO | S_IWUSR, dec_option_show, dec_option_store),
    __ATTR(print_flag, S_IRUGO | S_IWUSR, print_flag_show, print_flag_store),
    __ATTR(ac3_drc_control, S_IRUGO | S_IWUSR | S_IWGRP, ac3_drc_control_show, ac3_drc_control_store),
    __ATTR(dsp_debug, S_IRUGO | S_IWUSR, dsp_debug_show, dsp_debug_store),
    __ATTR(dts_dec_control, S_IRUGO | S_IWUSR, dts_dec_control_show, dts_dec_control_store),   
    __ATTR(skip_rawbytes, S_IRUGO | S_IWUSR, skip_rawbytes_show, skip_rawbytes_store),
    __ATTR_RO(pcm_left_len),
    __ATTR_NULL
};

#ifdef CONFIG_PM
static int audiodsp_suspend(struct device* dev, pm_message_t state)
{
    /* struct audiodsp_priv *priv = */audiodsp_privdata();
#if 0	 
    if(wake_lock_active(&priv->wakelock)){
        return -1; // please stop dsp first
    }
#endif	
    pm_state.event = state.event;
    if(state.event == PM_EVENT_SUSPEND){
        // should sleep cpu2 here after RevC chip
        //msleep(50);
    }
    printk("audiodsp suspend\n");
    return 0;
}

static int audiodsp_resume(struct device* dev)
{
    if(pm_state.event == PM_EVENT_SUSPEND){
        // wakeup cpu2 
        pm_state.event = -1;
    }
    printk("audiodsp resumed\n");
    return 0;
}
#endif

static struct class audiodsp_class = {
    .name = DSP_DRIVER_NAME,
    .class_attrs = audiodsp_attrs,
#ifdef CONFIG_PM
    .suspend = audiodsp_suspend,
    .resume = audiodsp_resume,
#else
    .suspend = NULL,
    .resume = NULL,
#endif
};

int audiodsp_probe(void )
{

	int res=0;
	struct audiodsp_priv *priv;
    dsp_debug_flag = 1;
	priv=kmalloc(sizeof(struct audiodsp_priv),GFP_KERNEL);
	if(priv==NULL)
		{
		DSP_PRNT("Out of memory for audiodsp register\n");
		return -1;
		}
    priv->dsp_is_started=0;
/*
    priv->p = ioremap_nocache(AUDIO_DSP_START_PHY_ADDR, S_1M);
    if(priv->p)
        DSP_PRNT("DSP IOREMAP to addr 0x%x\n",(unsigned)priv->p);
    else{
        DSP_PRNT("DSP IOREMAP error\n");
        goto error1;
    }
*/    
	audiodsp_p=priv;
	audiodsp_init_mcode(priv);
	if(priv->dsp_heap_size){
		if(priv->dsp_heap_start==0)
			priv->dsp_heap_start=(unsigned long)kmalloc(priv->dsp_heap_size,GFP_KERNEL);
		if(priv->dsp_heap_start==0)
		{
			DSP_PRNT("kmalloc error,no memory for audio dsp dsp_set_heap\n");
			kfree(priv);
			return -ENOMEM;
		}
	}	
	res = register_chrdev(AUDIODSP_MAJOR, DSP_NAME, &audiodsp_fops);
	if (res < 0) {
		DSP_PRNT("Can't register  char devie for " DSP_NAME "\n");
		goto error1;
	} else {
		DSP_PRNT("register " DSP_NAME " to char divece(%d)\n",
			  AUDIODSP_MAJOR);
	}

	res = class_register(&audiodsp_class);
	if(res <0 ){
	    DSP_PRNT("Create audiodsp class failed\n");
	    res = -EEXIST;
	    goto error2;
	}
	priv->class = &audiodsp_class;
	priv->dev = device_create(priv->class,
					    NULL, MKDEV(AUDIODSP_MAJOR, 0),
					    NULL, "audiodsp0");
	if(priv->dev==NULL)
		{
		res = -EEXIST;
		goto error3;
		}
	audiodsp_init_mailbox(priv);
	init_audiodsp_monitor(priv);
	wake_lock_init(&priv->wakelock,WAKE_LOCK_SUSPEND, "audiodsp");
#ifdef CONFIG_AM_STREAMING	
	set_adec_func(audiodsp_get_status);
#endif
    memset((void*)DSP_REG_OFFSET,0,REG_MEM_SIZE);
#ifndef CONFIG_MESON_TRUSTZONE    
#if ((MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6)||(MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TV))
    dsp_get_debug_interface(0);    
#elif MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
	// to do
#endif
#endif
    return res;

//error4:
	device_destroy(priv->class, MKDEV(AUDIODSP_MAJOR, 0));
error3:
	class_destroy(priv->class);
error2:
	unregister_chrdev(AUDIODSP_MAJOR, DSP_NAME);
error1:
	kfree(priv);
	return res;
}

struct audiodsp_priv *audiodsp_privdata(void)
{
	return audiodsp_p;
}


static int __init audiodsp_init_module(void)
{

	return audiodsp_probe();
}

static void __exit audiodsp_exit_module(void)
{
	struct audiodsp_priv *priv;
	priv=audiodsp_privdata();
#ifdef CONFIG_AM_STREAMING		
	set_adec_func(NULL);
#endif
	dsp_stop(priv);
	stop_audiodsp_monitor(priv);
	audiodsp_release_mailbox(priv);
	release_audiodsp_monitor(priv);
	audiodsp_microcode_free(priv);
  /*
    iounmap(priv->p);
  */
	device_destroy(priv->class, MKDEV(AUDIODSP_MAJOR, 0));
	class_destroy(priv->class);
	unregister_chrdev(AUDIODSP_MAJOR, DSP_NAME);
	kfree(priv);
	priv=NULL;
	return;
}

module_init(audiodsp_init_module);
module_exit(audiodsp_exit_module);
