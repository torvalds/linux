#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/amlogic/amports/tsync.h>
#include <linux/platform_device.h>
#include <linux/amlogic/amports/timestamp.h>
#include <linux/amlogic/amports/ptsserv.h>
#include "tsync_pcr.h"
#include "amvdec.h"
#include "tsdemux.h"
#include "streambuf.h"
#include "amports_priv.h"

//#define CONFIG_AM_PCRSYNC_LOG

#ifdef CONFIG_AM_PCRSYNC_LOG
#define AMLOG
#define LOG_LEVEL_ERROR     0
#define LOG_LEVEL_ATTENTION 1
#define LOG_LEVEL_INFO      2
#define LOG_LEVEL_VAR       amlog_level_tsync_pcr
#define LOG_MASK_VAR        amlog_mask_tsync_pcr
#endif
#include <linux/amlogic/amlog.h>
MODULE_AMLOG(AMLOG_DEFAULT_LEVEL, 0, LOG_DEFAULT_LEVEL_DESC, LOG_DEFAULT_MASK_DESC);

typedef enum {
    PLAY_MODE_NORMAL=0,
    PLAY_MODE_SLOW,
    PLAY_MODE_SPEED,
    PLAY_MODE_FORCE_SLOW,
    PLAY_MODE_FORCE_SPEED,
} play_mode_t;

#define CHECK_INTERVAL  (HZ * 5)

#define START_AUDIO_LEVEL       256
#define START_VIDEO_LEVEL       2048
#define PAUSE_AUDIO_LEVEL         16
#define PAUSE_VIDEO_LEVEL         512
#define UP_RESAMPLE_AUDIO_LEVEL      128
#define UP_RESAMPLE_VIDEO_LEVEL      1024
#define DOWN_RESAMPLE_CACHE_TIME     90000*2
#define NO_DATA_CHECK_TIME           4000

/* the diff of system time and referrence lock, which use the threshold to adjust the system time  */
#define OPEN_RECOVERY_THRESHOLD 18000
#define CLOSE_RECOVERY_THRESHOLD 300
#define RECOVERY_SPAN 10
#define FORCE_RECOVERY_SPAN 20

/* the delay from ts demuxer to the amvideo  */
#define DEFAULT_VSTREAM_DELAY 18000

static struct timer_list tsync_pcr_check_timer;

static u32 tsync_pcr_discontinue_threshold = (TIME_UNIT90K * 1.5);

static u32 tsync_pcr_ref_cache_time = TIME_UNIT90K;

static u32 tsync_pcr_system_startpcr=0;
static u32 tsync_pcr_tsdemux_startpcr=0;

static int tsync_pcr_vpause_flag = 0;
static int tsync_pcr_apause_flag = 0;
static int tsync_pcr_vstart_flag = 0;
static int tsync_pcr_inited_flag = 0;

// the really ts demuxer pcr, haven't delay
static u32 tsync_pcr_last_tsdemuxpcr = 0;
static u32 tsync_pcr_discontinue_point = 0;
static u32 tsync_pcr_discontinue_waited = 0;							// the time waited the v-discontinue to happen
static int tsync_pcr_tsdemuxpcr_discontinue = 0;						// the boolean value		

static int abuf_level=0;
static int abuf_size=0;
static int vbuf_level=0;
static int vbuf_size=0;
static int play_mode=PLAY_MODE_NORMAL;
//static int tsync_pcr_debug_pcrscr = 100;

extern int get_vsync_pts_inc_mode(void);

u32 tsync_pcr_vstream_delayed(void)
{
    int cur_delay = calculation_vcached_delayed();	
    if(cur_delay == -1)
    	return DEFAULT_VSTREAM_DELAY;

    return cur_delay;
}

void tsync_pcr_avevent_locked(avevent_t event, u32 param)
{

    switch (event) {
    case VIDEO_START:        
        if (tsync_pcr_vstart_flag == 0) {
            timestamp_firstvpts_set(param);
        }

        if(tsync_pcr_vstart_flag == 0 && tsync_pcr_inited_flag == 0){		
		u32 ref_pcr=param - VIDEO_HOLD_THRESHOLD;		// to wait 3 second
		u32 tsdemux_pcr = tsdemux_pcrscr_get();
		timestamp_pcrscr_set(ref_pcr);						

		tsync_pcr_tsdemux_startpcr = tsdemux_pcr;
		tsync_pcr_system_startpcr = ref_pcr;
		printk("video start! init system time param=%x ref_pcr= %x  \n",param,ref_pcr);

        	if (!tsync_pcr_vpause_flag) {
            		timestamp_pcrscr_enable(1);
        	}
        }
        tsync_pcr_vstart_flag=1;
	 break;

    case VIDEO_STOP:
	 timestamp_pcrscr_enable(0);
	 timestamp_vpts_set(0);
	 timestamp_firstvpts_set(0);
	 //tsync_pcr_debug_pcrscr=100;

	 tsync_pcr_vpause_flag=0;
	 tsync_pcr_vstart_flag=0;
	 tsync_pcr_inited_flag=0;
	 
	 tsync_pcr_tsdemuxpcr_discontinue=0;
	 tsync_pcr_discontinue_point=0;
	 tsync_pcr_discontinue_waited=0;

	 tsync_pcr_tsdemux_startpcr = 0;
	 tsync_pcr_system_startpcr = 0;
	 printk("video stop! \n");
        break;
        
    case VIDEO_TSTAMP_DISCONTINUITY:  	 
    	{		    	
		unsigned oldpts=timestamp_vpts_get();
		if((abs(param-oldpts)>AV_DISCONTINUE_THREDHOLD_MIN) && (!get_vsync_pts_inc_mode())){
	    	u32 tsdemux_pcr = tsdemux_pcrscr_get();
	    	u32 ref_pcr = param;
			printk("[tsync_pcr_avevent_locked] video discontinue happen.param=%x,discontinue=%d\n",param,tsync_pcr_tsdemuxpcr_discontinue);
		    	//if(ref_pcr == 0)
		    	//	ref_pcr=tsdemux_pcr-tsync_pcr_vstream_delayed();
			timestamp_pcrscr_set(ref_pcr);

			tsync_pcr_tsdemux_startpcr = tsdemux_pcr;
			tsync_pcr_system_startpcr = ref_pcr;
			printk("reset ref pcr=%x , ts demuxer pcr=%x \n",tsync_pcr_system_startpcr, tsync_pcr_tsdemux_startpcr);

		    	/* to resume the pcr check*/
			tsync_pcr_tsdemuxpcr_discontinue=0;	 
		    	tsync_pcr_discontinue_point=0;
		    	tsync_pcr_discontinue_waited=0;
		}
		timestamp_vpts_set(param);

		break;
    	} 
    case AUDIO_PRE_START:
        timestamp_apts_start(0);
        printk("audio prestart!   \n");
        break;

    case AUDIO_START:	
        if(timestamp_apts_started()==0)
        	timestamp_firstapts_set(param);
    	
	 timestamp_apts_set(param);
        timestamp_apts_enable(1);
        timestamp_apts_start(1);
        
        tsync_pcr_apause_flag=0;
        printk("audio start!timestamp_apts_set =%x.   \n",param);
        break;

    case AUDIO_RESUME:
	 timestamp_apts_enable(1);
        tsync_pcr_apause_flag=0;
        printk("audio resume!   \n");
        break;

    case AUDIO_STOP:
	 timestamp_apts_enable(0);
	 timestamp_apts_set(-1);
        timestamp_apts_start(0);
        timestamp_firstapts_set(0);
        tsync_pcr_apause_flag=0;
        printk("audio stop!   \n");
        break;

    case AUDIO_PAUSE:
  	 timestamp_apts_enable(0);
        tsync_pcr_apause_flag=1;
        printk("audio pause!   \n");
        break;

    case VIDEO_PAUSE:
       if(param == 1){
      	 	timestamp_pcrscr_enable(0);
      	 	tsync_pcr_vpause_flag = 1;
		printk("video pause!\n");
	 }else{
		timestamp_pcrscr_enable(1);
		tsync_pcr_vpause_flag = 0;
		printk("video resume\n");
	 }
        break;	

    default:
        break;
    }
    switch (event) {
    case VIDEO_START:
    case AUDIO_START:
    case AUDIO_RESUME:
        amvdev_resume();
        break;
    case VIDEO_STOP:
    case AUDIO_STOP:
    case AUDIO_PAUSE:
        amvdev_pause();
        break;
    case VIDEO_PAUSE:
        if (tsync_pcr_vpause_flag)
            amvdev_pause();
        else
            amvdev_resume();
        break;
    default:
        break;
    }

}

// timer to check the system with the referrence time in ts stream.
static unsigned long tsync_pcr_check(void)
{
    u32 tsdemux_pcr=tsdemux_pcrscr_get();
    u32 tsdemux_pcr_diff=0;
    int need_recovery=1;
    unsigned long res=jiffies;

   /* check the value valid */
   if(tsync_pcr_last_tsdemuxpcr ==0 && tsdemux_pcr ==0)
   	return res;

    // To monitor the pcr discontinue 
    tsdemux_pcr_diff=abs(tsdemux_pcr - tsync_pcr_last_tsdemuxpcr);
    if(tsdemux_pcr_diff > tsync_pcr_discontinue_threshold && tsync_pcr_tsdemuxpcr_discontinue==0 && tsync_pcr_inited_flag==1){
    	tsync_pcr_tsdemuxpcr_discontinue=1;  
    	tsync_pcr_discontinue_waited=tsync_pcr_vstream_delayed()+TIME_UNIT90K;
    	printk("[tsync_pcr_check] refpcr_discontinue. tsdemux_pcr_diff=%x, last refpcr=%x, repcr=%x\n",tsdemux_pcr_diff,tsync_pcr_last_tsdemuxpcr,tsdemux_pcr);
	tsync_pcr_discontinue_point=timestamp_pcrscr_get();
	need_recovery=0;
    }
    else if(tsync_pcr_tsdemuxpcr_discontinue == 1){
    	// to pause the pcr check
	if(abs(timestamp_pcrscr_get()-tsync_pcr_discontinue_point)>tsync_pcr_discontinue_waited){			
		// the v-discontinue did'n happen
	 	tsync_pcr_tsdemuxpcr_discontinue=0;	 
    	 	tsync_pcr_discontinue_point=0;
    	 	tsync_pcr_discontinue_waited=0;	
    	 	printk("[tsync_pcr_check] video discontinue didn't happen, waited=%lx\n",abs(tsdemux_pcr-tsync_pcr_discontinue_point));
	}
	need_recovery=0;
    }
    tsync_pcr_last_tsdemuxpcr=tsdemux_pcr;

    abuf_level= stbuf_level(get_buf_by_type(BUF_TYPE_AUDIO));
    abuf_size= stbuf_size(get_buf_by_type(BUF_TYPE_AUDIO));
    vbuf_level= stbuf_level(get_buf_by_type(BUF_TYPE_VIDEO));
    vbuf_size= stbuf_size(get_buf_by_type(BUF_TYPE_VIDEO));
    if(tsync_pcr_inited_flag == 0){
	// check the video and audio stream buffer, to check to start
	if((timestamp_apts_started() == 1 && tsync_pcr_vstart_flag)){
	    	u32 ref_pcr =0;	
		//if(timestamp_firstvpts_get() <= timestamp_firstapts_get() || timestamp_firstapts_get() == 0){
			ref_pcr=timestamp_firstvpts_get();
			printk("[tsync_pcr_check]Inited use video pts. ref_pcr=%x tsdemux time=%x  \n",ref_pcr,tsdemux_pcr);			
		//}
		//else{
		//	ref_pcr=timestamp_firstapts_get();
		//	printk("[tsync_pcr_check]Inited use audio pts. ref_pcr=%x tsdemux time=%x  \n",ref_pcr,tsdemux_pcr);	
		//}
		
		timestamp_pcrscr_set(ref_pcr);
		tsync_pcr_tsdemux_startpcr = tsdemux_pcr;
		tsync_pcr_system_startpcr = ref_pcr;
		tsync_pcr_inited_flag = 1;
		play_mode=PLAY_MODE_FORCE_SLOW;
		printk("[tsync_pcr_check] init and slow play.abuf_level=%x vbuf_level=%x \n", abuf_level,vbuf_level);	
		
        	if (!tsync_pcr_vpause_flag) {
            		timestamp_pcrscr_enable(1);
        	}	
	}

	return res;
    }

    if(!tsync_pcr_vpause_flag){
	if(vbuf_level < PAUSE_VIDEO_LEVEL||abuf_level<PAUSE_AUDIO_LEVEL){
		tsync_pcr_avevent_locked(VIDEO_PAUSE,1);						// to pause
		printk("[tsync_pcr_check] to pause abuf_level=%x vbuf_level=%x play_mode=%d \n",abuf_level,vbuf_level,play_mode);
		return res;
	}	
    }else{ 	
    	if(vbuf_level < START_VIDEO_LEVEL||abuf_level < START_AUDIO_LEVEL)
    		return res;

	printk("[tsync_pcr_check] resume and show play. abuf_level=%x vbuf_level=%x play_mode=%d\n",abuf_level,vbuf_level,play_mode);
    	// to resume
	tsync_pcr_avevent_locked(VIDEO_PAUSE,0);
    	play_mode=PLAY_MODE_FORCE_SLOW;
    }

    if((vbuf_level * 5 > vbuf_size * 4 || abuf_level * 5 > abuf_size * 4) && play_mode != PLAY_MODE_FORCE_SPEED){
    	printk("[tsync_pcr_check]Buffer will overflow and speed play. vlevel=%x vsize=%x alevel=%x asize=%x play_mode=%d\n",
    		vbuf_level,vbuf_size,abuf_level,abuf_size, play_mode);
	// the video stream buffer will happen overflow
    	play_mode=PLAY_MODE_FORCE_SPEED;
    }

    if(play_mode == PLAY_MODE_FORCE_SLOW){
    	if((vbuf_level * 50 > vbuf_size && abuf_level * 50 > abuf_size)||			
    	    vbuf_level * 10 > vbuf_size ||
	    abuf_level * 10 > abuf_size){
	    	play_mode=PLAY_MODE_NORMAL;
		printk("[tsync_pcr_check]Buffer to vlevel=%x vsize=%x alevel=%x asize=%x. slow to normal play\n",
			vbuf_level,vbuf_size,abuf_level,abuf_size);	  
    	}
    }
    else if(play_mode == PLAY_MODE_FORCE_SPEED){
	if((vbuf_level * 3 < vbuf_size && abuf_level * 3 < abuf_size) ||
	    vbuf_level * 10 < vbuf_size ||
	    abuf_level * 10 < abuf_size){
		play_mode=PLAY_MODE_NORMAL;
		tsync_pcr_tsdemux_startpcr = tsdemux_pcr;
		tsync_pcr_system_startpcr = timestamp_pcrscr_get();
		printk("[tsync_pcr_check]Buffer to vlevel=%x vsize=%x alevel=%x asize=%x. speed to normal play\n",
			vbuf_level,vbuf_size,abuf_level,abuf_size);
	}
    }
/*
    tsync_pcr_debug_pcrscr++;
    if(tsync_pcr_debug_pcrscr>=100){    	
    	printk("[tsync_pcr_check]debug pcr=%x,refer lock=%x, vpts =%x, apts=%x\n",pcr,tsdemux_pcr,timestamp_vpts_get(),timestamp_apts_get());
    	tsync_pcr_debug_pcrscr=0;
    }
*/

    //if(need_recovery==1 || play_mode == PLAY_MODE_FORCE_SLOW || play_mode == PLAY_MODE_FORCE_SPEED){
    /* To check the system time with ts demuxer pcr */
    if(play_mode != PLAY_MODE_FORCE_SLOW && play_mode != PLAY_MODE_FORCE_SPEED){
	    u32 ref_pcr=tsdemux_pcr-tsync_pcr_ref_cache_time;
	    u32 cur_pcr=timestamp_pcrscr_get();
	    u32 diff=abs(ref_pcr - cur_pcr);
	    if(diff > OPEN_RECOVERY_THRESHOLD && cur_pcr<ref_pcr && play_mode!=PLAY_MODE_SPEED && need_recovery){
		play_mode=	PLAY_MODE_SPEED;
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] diff=%x to speed play  \n",diff);	
	    }
	    else if(diff > OPEN_RECOVERY_THRESHOLD && cur_pcr>ref_pcr && play_mode!=PLAY_MODE_SLOW && need_recovery){
		play_mode=PLAY_MODE_SLOW;
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] diff=%x to show play  \n",diff);	
	    }	
	    else if(diff < CLOSE_RECOVERY_THRESHOLD && play_mode!=PLAY_MODE_NORMAL){
	    	play_mode=PLAY_MODE_NORMAL;
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] diff=%x to nomal play  \n",diff);	
	    }
    }
    
    if(play_mode == PLAY_MODE_SLOW)
    	timestamp_pcrscr_set(timestamp_pcrscr_get()-RECOVERY_SPAN);
    else if( play_mode == PLAY_MODE_FORCE_SLOW)
    	timestamp_pcrscr_set(timestamp_pcrscr_get()-FORCE_RECOVERY_SPAN);
    else if(play_mode == PLAY_MODE_SPEED)
    	timestamp_pcrscr_set(timestamp_pcrscr_get()+RECOVERY_SPAN);
    else if( play_mode == PLAY_MODE_FORCE_SPEED)
    	timestamp_pcrscr_set(timestamp_pcrscr_get()+FORCE_RECOVERY_SPAN);
    //}

    return res;
}

static void tsync_pcr_check_timer_func(unsigned long arg)
{
    if(tsdemux_pcrscr_valid() == 1){
	tsync_pcr_check_timer.expires = tsync_pcr_check();
    }
    else{
    	    tsync_pcr_last_tsdemuxpcr=0;
    	    tsync_pcr_check_timer.expires = jiffies;
    }
    
    add_timer(&tsync_pcr_check_timer);
}

static ssize_t show_play_mode(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%d\n", play_mode);
}


// --------------------------------------------------------------------------------
// define of tsync pcr module

static struct class_attribute tsync_pcr_class_attrs[] = {
    __ATTR(play_mode,  S_IRUGO | S_IWUSR | S_IWGRP, show_play_mode, NULL),
    __ATTR_NULL
};
static struct class tsync_pcr_class = {
        .name = "tsync_pcr",
        .class_attrs = tsync_pcr_class_attrs,
    };

static int __init tsync_pcr_init(void)
{
    int r;

    r = class_register(&tsync_pcr_class);

    if (r) {
        printk("[tsync_pcr_init]tsync_pcr_class create fail.  \n");
        return r;
    }

    /* init audio pts to -1, others to 0 */
    timestamp_apts_set(-1);
    timestamp_vpts_set(0);
    timestamp_pcrscr_set(0);

    init_timer(&tsync_pcr_check_timer);

    tsync_pcr_check_timer.function = tsync_pcr_check_timer_func;
    tsync_pcr_check_timer.expires = jiffies;

    add_timer(&tsync_pcr_check_timer);

    printk("[tsync_pcr_init]init success. \n");
    return (0);
}

static void __exit tsync_pcr_exit(void)
{
    del_timer_sync(&tsync_pcr_check_timer);

    class_unregister(&tsync_pcr_class);
    printk("[tsync_pcr_exit]exit success.   \n");
}


module_init(tsync_pcr_init);
module_exit(tsync_pcr_exit);

MODULE_DESCRIPTION("AMLOGIC time sync management driver of referrence by pcrscr");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("le yang <le.yang@amlogic.com>");
