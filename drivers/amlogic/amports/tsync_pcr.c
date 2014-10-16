#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>

#include <linux/amlogic/amports/tsync.h>
#include <linux/platform_device.h>
#include <linux/amlogic/amports/timestamp.h>
#include <linux/amlogic/amports/ptsserv.h>

#include "tsync_pcr.h"
#include "amvdec.h"
#include "tsdemux.h"
#include "streambuf.h"
#include "amports_priv.h"

#define CONFIG_AM_PCRSYNC_LOG

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
#define START_VIDEO_LEVEL       20480
#define PAUSE_AUDIO_LEVEL         16
#define PAUSE_VIDEO_LEVEL         256
#define UP_RESAMPLE_AUDIO_LEVEL      128
#define UP_RESAMPLE_VIDEO_LEVEL      1024
#define DOWN_RESAMPLE_CACHE_TIME     90000*2
#define NO_DATA_CHECK_TIME           4000

/* the diff of system time and referrence lock, which use the threshold to adjust the system time  */
#define OPEN_RECOVERY_THRESHOLD 18000
#define CLOSE_RECOVERY_THRESHOLD 300
#define RECOVERY_SPAN 5
#define FORCE_RECOVERY_SPAN 20

/* the delay from ts demuxer to the amvideo  */
#define DEFAULT_VSTREAM_DELAY 18000

#define RESAMPLE_TYPE_NONE      0
#define RESAMPLE_TYPE_DOWN      1
#define RESAMPLE_TYPE_UP        2
#define RESAMPLE_DOWN_FORCE_PCR_SLOW 3

#define MS_INTERVAL  (HZ/1000)		
#define TEN_MS_INTERVAL  (HZ/100)		

// ------------------------------------------------------------------
// The const 

static u32 tsync_pcr_discontinue_threshold = (TIME_UNIT90K * 1.5);
static u32 tsync_pcr_ref_latency = 30000;				//TIME_UNIT90K/3

// use for pcr valid mode
static u32 tsync_pcr_max_cache_time = TIME_UNIT90K*2;				//TIME_UNIT90K*2;
static u32 tsync_pcr_up_cache_time = TIME_UNIT90K*1.5;				//TIME_UNIT90K*1.5;
static u32 tsync_pcr_down_cache_time = TIME_UNIT90K*1.2;			//TIME_UNIT90K*1.2;
static u32 tsync_pcr_min_cache_time = TIME_UNIT90K*0.8;			//TIME_UNIT90K*0.8;


// use for pcr invalid mode
static u32 tsync_pcr_max_delay_time = TIME_UNIT90K*3;				//TIME_UNIT90K*3;
static u32 tsync_pcr_up_delay_time = TIME_UNIT90K*2;				//TIME_UNIT90K*2;
static u32 tsync_pcr_down_delay_time = TIME_UNIT90K*1.5;			//TIME_UNIT90K*1.5;
static u32 tsync_pcr_min_delay_time = TIME_UNIT90K*1;				//TIME_UNIT90K*0.8;

// ------------------------------------------------------------------
// The variate

static struct timer_list tsync_pcr_check_timer;

static u32 tsync_pcr_system_startpcr=0;
static u32 tsync_pcr_tsdemux_startpcr=0;

static int tsync_pcr_vpause_flag = 0;
static int tsync_pcr_apause_flag = 0;
static int tsync_pcr_vstart_flag = 0;
static int tsync_pcr_astart_flag = 0;
static int tsync_pcr_inited_flag = 0;

// the really ts demuxer pcr, haven't delay
static u32 tsync_pcr_last_tsdemuxpcr = 0;
static u32 tsync_pcr_discontinue_local_point = 0;
static u32 tsync_pcr_discontinue_waited = 0;							// the time waited the v-discontinue to happen
static u8 tsync_pcr_tsdemuxpcr_discontinue = 0;						// the boolean value		
static u32 tsync_pcr_discontinue_point = 0;

static int abuf_level=0;
static int abuf_size=0;
static int vbuf_level=0;
static int vbuf_size=0;
static int play_mode=PLAY_MODE_NORMAL;
static u8 tsync_pcr_started=0;
static int tsync_pcr_read_cnt=0;
static u8 tsync_pcr_usepcr=1;
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
            //play_mode=PLAY_MODE_NORMAL;
            printk("video start! init system time param=%x cur_pcr=%x\n",param,timestamp_pcrscr_get());
        }
/*
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
        }*/
        tsync_pcr_vstart_flag=1;
	 break;

    case VIDEO_STOP:
	 timestamp_pcrscr_enable(0);
	 timestamp_vpts_set(0);
	 //tsync_pcr_debug_pcrscr=100;

	 tsync_pcr_vpause_flag=0;
	 tsync_pcr_vstart_flag=0;
	 tsync_pcr_inited_flag=0;
	 
	 tsync_pcr_tsdemuxpcr_discontinue=0;
	 tsync_pcr_discontinue_point=0;
	 tsync_pcr_discontinue_local_point=0;
	 tsync_pcr_discontinue_waited=0;

	 tsync_pcr_tsdemux_startpcr = 0;
	 tsync_pcr_system_startpcr = 0;
	 play_mode=PLAY_MODE_NORMAL;
	 printk("video stop! \n");
        break;
        
    case VIDEO_TSTAMP_DISCONTINUITY:  	 
    	{		    	
		//unsigned oldpts=timestamp_vpts_get();
		u32 tsdemux_pcr = tsdemux_pcrscr_get();
		//if((abs(param-oldpts)>AV_DISCONTINUE_THREDHOLD_MIN) && (!get_vsync_pts_inc_mode())){
		if(!get_vsync_pts_inc_mode()){
		    	u32 ref_pcr = param-tsync_pcr_ref_latency*2;
		    	//if(ref_pcr == 0)
		    	//	ref_pcr=tsdemux_pcr-tsync_pcr_vstream_delayed();
			
			timestamp_pcrscr_set(ref_pcr);

			tsync_pcr_tsdemux_startpcr = tsdemux_pcr;
			tsync_pcr_system_startpcr = ref_pcr;
			//play_mode=PLAY_MODE_FORCE_SLOW;
			printk("[tsync_pcr_avevent_locked] video discontinue happen and slow play.ref_pcr=%x,param=%x,discontinue=%d\n",ref_pcr,param,tsync_pcr_tsdemuxpcr_discontinue);

		    	/* to resume the pcr check*/
			tsync_pcr_tsdemuxpcr_discontinue=0;	 
		    	tsync_pcr_discontinue_point=0;
			tsync_pcr_discontinue_local_point=0;
		    	tsync_pcr_discontinue_waited=0;
		}
		//}
		timestamp_vpts_set(param);

		break;
    	} 
    case AUDIO_PRE_START:
        timestamp_apts_start(0);
        tsync_pcr_astart_flag=0;
        printk("audio prestart!   \n");
        break;

    case AUDIO_START:		
	 timestamp_apts_set(param);
        timestamp_apts_enable(1);
        timestamp_apts_start(1);

        tsync_pcr_astart_flag=1;
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
        tsync_pcr_astart_flag=0;
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
    u32 tsdemux_pcr=0;
    u32 tsdemux_pcr_diff=0;
    int need_recovery=1;
    unsigned long res=1;
    if(tsync_get_mode() != TSYNC_MODE_PCRMASTER){
	return res;
    }

    tsdemux_pcr=tsdemux_pcrscr_get();
    if(tsync_pcr_usepcr==1){
	// To monitor the pcr discontinue 
	tsdemux_pcr_diff=abs(tsdemux_pcr - tsync_pcr_last_tsdemuxpcr);
	if(tsync_pcr_last_tsdemuxpcr!=0&&tsdemux_pcr!=0&&tsdemux_pcr_diff > tsync_pcr_discontinue_threshold && tsync_pcr_inited_flag==1){
		u32 video_delayed=0;
		tsync_pcr_tsdemuxpcr_discontinue=1;
		video_delayed = tsync_pcr_vstream_delayed();
		if(TIME_UNIT90K*2<=video_delayed&&video_delayed<=TIME_UNIT90K*4)  
			tsync_pcr_discontinue_waited=video_delayed+TIME_UNIT90K;
		else if(TIME_UNIT90K*2>video_delayed)  
			tsync_pcr_discontinue_waited=TIME_UNIT90K*3;
		else 
			tsync_pcr_discontinue_waited=TIME_UNIT90K*5;
			
		printk("[tsync_pcr_check] refpcr_discontinue. tsdemux_pcr_diff=%x, last refpcr=%x, repcr=%x,waited=%x\n",tsdemux_pcr_diff,tsync_pcr_last_tsdemuxpcr,tsdemux_pcr,tsync_pcr_discontinue_waited);
		tsync_pcr_discontinue_local_point=timestamp_pcrscr_get();
		tsync_pcr_discontinue_point=tsdemux_pcr-tsync_pcr_ref_latency;
		need_recovery=0;
	}
	else if(tsync_pcr_tsdemuxpcr_discontinue == 1){
		// to pause the pcr check
		if(abs(timestamp_pcrscr_get()-tsync_pcr_discontinue_local_point)>tsync_pcr_discontinue_waited){			
		 	printk("[tsync_pcr_check] video discontinue didn't happen, waited=%x\n",tsync_pcr_discontinue_waited);
			// the v-discontinue did'n happen
	 		tsync_pcr_tsdemuxpcr_discontinue=0;	 
		 	tsync_pcr_discontinue_point=0;
		 	tsync_pcr_discontinue_local_point=0;
		 	tsync_pcr_discontinue_waited=0;	
		}
		need_recovery=0;
	}
	tsync_pcr_last_tsdemuxpcr=tsdemux_pcr;
    }

    abuf_level= stbuf_level(get_buf_by_type(BUF_TYPE_AUDIO));
    abuf_size= stbuf_size(get_buf_by_type(BUF_TYPE_AUDIO));
    vbuf_level= stbuf_level(get_buf_by_type(BUF_TYPE_VIDEO));
    vbuf_size= stbuf_size(get_buf_by_type(BUF_TYPE_VIDEO));
    if(tsync_pcr_inited_flag == 0){
	// check the video and audio stream buffer, to check to start
	u32 first_pcr =tsdemux_first_pcrscr_get();	
    	u32 first_vpts = timestamp_firstvpts_get();
	u32 first_apts = timestamp_firstapts_get();
	u32 ref_pcr=0;
	u8 ref_pcr_valid=0;

	if(tsync_pcr_usepcr==1){
		if(first_pcr != 0){
			// pcr is valid, use 
			ref_pcr = first_pcr - tsync_pcr_ref_latency;
			ref_pcr_valid=1;
			printk("[tsync_pcr_check]Inited use pcr mode.ref_pcr=%x first_pcr=%x first_vpts=%x first_apts=%x \n",ref_pcr,first_pcr,first_vpts,first_apts);
		}else{
			if(first_vpts!=0||first_apts!=0){
				tsync_pcr_usepcr=0;
				printk("[tsync_pcr_check]can't read valid pcr, use other mode. read_cnt=%d \n",tsync_pcr_read_cnt);
			}
			tsync_pcr_read_cnt++;
		}	
	}
	else{ 
		// pcr is invalid, use vmaster or amaster mode
		if(tsync_pcr_astart_flag==1&&tsync_pcr_vstart_flag==1&&first_vpts!=0&&first_apts!=0&&vbuf_level>=START_VIDEO_LEVEL){
			if(first_vpts <= first_apts){
				//play_mode=PLAY_MODE_FORCE_SLOW;
				ref_pcr = first_vpts;
				printk("[tsync_pcr_check]Inited use video pts and slow play.ref_pcr=%x first_pcr=%x first_vpts=%x first_apts=%x \n",ref_pcr,first_pcr,first_vpts,first_apts);				
			}
			else{
				//play_mode=PLAY_MODE_FORCE_SLOW;
				ref_pcr=first_apts;
				printk("[tsync_pcr_check]Inited use audio pts and slow play.ref_pcr=%x first_pcr=%x first_vpts=%x first_apts=%x \n",ref_pcr,first_pcr,first_vpts,first_apts);	
			}
			ref_pcr_valid=1;
		}
		else if(tsync_pcr_astart_flag==0&&tsync_pcr_vstart_flag==1&&first_vpts!=0&&(vbuf_level*20)>vbuf_size&&abuf_level==0){
			int vdelayed=calculation_vcached_delayed();
			if(tsync_pcr_max_cache_time<vdelayed){
				//play_mode=PLAY_MODE_FORCE_SLOW;
				ref_pcr = first_vpts;
				printk("[tsync_pcr_check]No audio.Inited video pts and slow play.ref_pcr=%x first_pcr=%x first_vpts=%x vdelayed=%x \n",ref_pcr,first_pcr,first_vpts,vdelayed);				
				ref_pcr_valid=1;
			}
       		}
       		else if(tsync_pcr_astart_flag==1&&tsync_pcr_vstart_flag==0&&first_apts!=0&&(abuf_level*20)>abuf_size&&vbuf_level==0){
       			int adelayed=calculation_acached_delayed();
			if(tsync_pcr_max_cache_time<adelayed){
				//play_mode=PLAY_MODE_FORCE_SLOW;
				ref_pcr = first_apts;
				ref_pcr_valid=1;
				printk("[tsync_pcr_check]No video.Inited audio pts and slow play.ref_pcr=%x first_pcr=%x adelayed=%x first_apts=%x \n",ref_pcr,first_pcr,adelayed,first_apts);				
			}
       		}
       }

	if(ref_pcr_valid==1){
		timestamp_pcrscr_set(ref_pcr);
		tsync_pcr_inited_flag = 1;
		printk("[tsync_pcr_check] inited.ref_pcr=%x abuf_level=%x vbuf_level=%x \n", ref_pcr,abuf_level,vbuf_level);	
		
	    	if (!tsync_pcr_vpause_flag) {
	        	timestamp_pcrscr_enable(1);
	    	}	
	}else{
		return res;
	}
    }    

    if(tsync_pcr_usepcr==0){
    	if(!tsync_pcr_vpause_flag){
		if(vbuf_level < PAUSE_VIDEO_LEVEL/*||abuf_level<PAUSE_AUDIO_LEVEL*/){
			tsync_pcr_avevent_locked(VIDEO_PAUSE,1);						// to pause
			printk("[tsync_pcr_check] to pause abuf_level=%x vbuf_level=%x play_mode=%d \n",abuf_level,vbuf_level,play_mode);
			return res;
		}	
    	}else{ 	
		if(vbuf_level < START_VIDEO_LEVEL/*||abuf_level < START_AUDIO_LEVEL*/)
			return res;

		printk("[tsync_pcr_check] resume and show play. abuf_level=%x vbuf_level=%x play_mode=%d\n",abuf_level,vbuf_level,play_mode);
		// to resume
		tsync_pcr_avevent_locked(VIDEO_PAUSE,0);
		play_mode=PLAY_MODE_FORCE_SLOW;
    	}
    }

    if((vbuf_level * 5 > vbuf_size * 4 || abuf_level * 5 > abuf_size * 4) && play_mode != PLAY_MODE_FORCE_SPEED){
	// the video stream buffer will happen overflow
	u32 new_pcr=0;
	play_mode=PLAY_MODE_FORCE_SPEED;
	new_pcr=timestamp_pcrscr_get()+72000;		// 90000*0.8
    	timestamp_pcrscr_set(new_pcr);
    	printk("[tsync_pcr_check]Buffer will overflow and speed play. new_pcr=%x vlevel=%x vsize=%x alevel=%x asize=%x play_mode=%d\n",
    		new_pcr,vbuf_level,vbuf_size,abuf_level,abuf_size, play_mode);
    }
 
    
    if(play_mode == PLAY_MODE_FORCE_SLOW){
    	/*if((vbuf_level * 50 > vbuf_size && abuf_level * 50 > abuf_size)||			
    	    vbuf_level * 20 > vbuf_size ||
	    abuf_level * 20 > abuf_size){*/
    	if(vbuf_level * 20 > vbuf_size){
	    	play_mode=PLAY_MODE_NORMAL;
		printk("[tsync_pcr_check]Buffer to vlevel=%x vsize=%x alevel=%x asize=%x. slow to normal play\n",
			vbuf_level,vbuf_size,abuf_level,abuf_size);	  
    	}
    }
    else if(play_mode == PLAY_MODE_FORCE_SPEED){
	if((vbuf_level * 4 < vbuf_size && abuf_level * 4 < abuf_size) ||
	    (vbuf_level * 4 < vbuf_size && abuf_level == 0)||
	    (abuf_level * 4 < abuf_size && vbuf_level == 0)){
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
    if((play_mode != PLAY_MODE_FORCE_SLOW) && (play_mode != PLAY_MODE_FORCE_SPEED) && (tsync_pcr_usepcr==1)){
	// use the pcr to adjust
	//u32 ref_pcr=tsdemux_pcr-calculation_vcached_delayed();
	int64_t ref_pcr=(int64_t)tsdemux_pcr- (int64_t)tsync_pcr_ref_latency;
	int64_t cur_pcr=(int64_t)timestamp_pcrscr_get();
	int64_t diff=abs(ref_pcr - cur_pcr);
	
	//if(diff > OPEN_RECOVERY_THRESHOLD && cur_pcr<ref_pcr && play_mode!=PLAY_MODE_SPEED && need_recovery){
	if(((ref_pcr -cur_pcr) > (tsync_pcr_max_cache_time))  && (play_mode!=PLAY_MODE_SPEED) && need_recovery){
		play_mode=PLAY_MODE_SPEED;
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] diff=%lld to speed play  \n",diff);	
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] ref_pcr=%lld to speed play  \n",ref_pcr);	
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] cur_pcr=%lld to speed play  \n",cur_pcr);	
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] tsync_pcr_max_cache_time=%d to speed play  \n",tsync_pcr_max_cache_time);	
	}
	//else if(diff > OPEN_RECOVERY_THRESHOLD && cur_pcr>ref_pcr && play_mode!=PLAY_MODE_SLOW && need_recovery){
	else if((ref_pcr - cur_pcr) < (tsync_pcr_min_cache_time) && (play_mode!=PLAY_MODE_SLOW) && need_recovery){
		play_mode=PLAY_MODE_SLOW;
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] diff=%lld to slow play  \n",diff);	
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] ref_pcr=%lld to slow play  \n",ref_pcr);	
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] cur_pcr=%lld to slow play  \n",cur_pcr);	
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] tsync_pcr_max_cache_time=%d to slow play  \n",tsync_pcr_max_cache_time);	
	}	
	//else if(diff < CLOSE_RECOVERY_THRESHOLD && play_mode!=PLAY_MODE_NORMAL){
	else if((!need_recovery||((tsync_pcr_down_cache_time<ref_pcr-cur_pcr)&&(ref_pcr-cur_pcr<tsync_pcr_up_cache_time)))&&(play_mode!=PLAY_MODE_NORMAL)){
		play_mode=PLAY_MODE_NORMAL;
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] ref_pcr=%lld to nomal play  \n",ref_pcr);	
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] cur_pcr=%lld to nomal play  \n",cur_pcr);	
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] tsync_pcr_max_cache_time=%d to nomal play  \n",tsync_pcr_max_cache_time);	
		amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] diff=%lld,need_recovery=%d to nomal play  \n",diff,need_recovery);	
	}
    }
    else if((play_mode != PLAY_MODE_FORCE_SLOW) && (play_mode != PLAY_MODE_FORCE_SPEED) && (tsync_pcr_usepcr==0)){
	// use the video cache time to adjust
	int video_cache_time = calculation_vcached_delayed();	
	if(video_cache_time > tsync_pcr_max_delay_time){
		if(play_mode!=PLAY_MODE_SPEED){
			play_mode=	PLAY_MODE_SPEED;
			amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] video_delay_time=%d to speed play  \n",video_cache_time);
		}
	}
	else if( video_cache_time < tsync_pcr_min_delay_time && video_cache_time>=0 ){
		if(play_mode!=PLAY_MODE_SLOW){
			play_mode=PLAY_MODE_SLOW;
			amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] video_delay_time=%d to show play  \n",video_cache_time);
		}
	}
	else{ 
		if(tsync_pcr_down_delay_time<=video_cache_time&&video_cache_time<=tsync_pcr_up_delay_time&&play_mode!=PLAY_MODE_NORMAL){
			play_mode=PLAY_MODE_NORMAL;
			amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] video_delay_time=%d to nomal play  \n", video_cache_time);	
		}
	}
    }
 
    if(need_recovery&&!tsync_pcr_vpause_flag){   
    	if(play_mode == PLAY_MODE_SLOW)
    		timestamp_pcrscr_set(timestamp_pcrscr_get()-RECOVERY_SPAN);
    	else if( play_mode == PLAY_MODE_FORCE_SLOW)
    		timestamp_pcrscr_set(timestamp_pcrscr_get()-FORCE_RECOVERY_SPAN);
    	else if(play_mode == PLAY_MODE_SPEED)
    		timestamp_pcrscr_set(timestamp_pcrscr_get()+RECOVERY_SPAN);
    	else if( play_mode == PLAY_MODE_FORCE_SPEED)
    		timestamp_pcrscr_set(timestamp_pcrscr_get()+FORCE_RECOVERY_SPAN);
    }
    //}

    return res;
}

static void tsync_pcr_check_timer_func(unsigned long arg)
{
    tsync_pcr_check();
    tsync_pcr_check_timer.expires = jiffies+TEN_MS_INTERVAL;
    
    add_timer(&tsync_pcr_check_timer);
}

static void tsync_pcr_param_reset(void){
	tsync_pcr_system_startpcr=0;
	tsync_pcr_tsdemux_startpcr=0;

	tsync_pcr_vpause_flag = 0;
	tsync_pcr_apause_flag = 0;
	tsync_pcr_vstart_flag = 0;
	tsync_pcr_astart_flag = 0;
	tsync_pcr_inited_flag = 0;

	tsync_pcr_last_tsdemuxpcr = 0;
	tsync_pcr_discontinue_local_point=0;
	tsync_pcr_discontinue_point = 0;
	tsync_pcr_discontinue_waited = 0;							// the time waited the v-discontinue to happen
	tsync_pcr_tsdemuxpcr_discontinue = 0;						// the boolean value		

	abuf_level=0;
	abuf_size=0;
	vbuf_level=0;
	vbuf_size=0;
	play_mode=PLAY_MODE_NORMAL;
	tsync_pcr_started=0;
}
int tsync_pcr_set_apts(unsigned pts)
{
	timestamp_apts_set(pts);
	//printk("[tsync_pcr_set_apts]set apts=%x",pts);
	return 0;
}
int tsync_pcr_start(void)
{
    tsync_pcr_param_reset();
    
    if(tsync_get_mode() == TSYNC_MODE_PCRMASTER){
    	printk("[tsync_pcr_start]PCRMASTER started success. \n");
	init_timer(&tsync_pcr_check_timer);

	tsync_pcr_check_timer.function = tsync_pcr_check_timer_func;
	tsync_pcr_check_timer.expires = jiffies;

	tsync_pcr_started=1;
	tsync_pcr_usepcr=tsdemux_pcrscr_valid();
	tsync_pcr_read_cnt=0;
    	printk("[tsync_pcr_start]usepcr=%d\n",tsync_pcr_usepcr);
	add_timer(&tsync_pcr_check_timer);
    }
    return 0;
}

void tsync_pcr_stop(void)
{
    if(tsync_pcr_started==1){
    	del_timer_sync(&tsync_pcr_check_timer);
    	printk("[tsync_pcr_start]PCRMASTER stop success. \n");
    }
    tsync_pcr_started=0;
}

// --------------------------------------------------------------------------------
// define of tsync pcr class node

static ssize_t show_play_mode(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%d\n", play_mode);
}

static ssize_t show_tsync_pcr_dispoint(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
printk("[%s:%d] tsync_pcr_discontinue_point:%x, HZ:%x, \n", __FUNCTION__, __LINE__, tsync_pcr_discontinue_point, HZ);
    return sprintf(buf, "0x%x\n", tsync_pcr_discontinue_point);
}

static ssize_t store_tsync_pcr_dispoint(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned pts;
    ssize_t r;

    r = sscanf(buf, "0x%x", &pts);
    if (r != 1) {
        return -EINVAL;
    }

    tsync_pcr_discontinue_point = pts;
	printk("[%s:%d] tsync_pcr_discontinue_point:%x, \n", __FUNCTION__, __LINE__, tsync_pcr_discontinue_point);

    return size;
}

static ssize_t store_tsync_pcr_audio_resample_type(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned type;
    ssize_t r;

    r = sscanf(buf, "%d", &type);
    if (r != 1) {
        return -EINVAL;
    }

    if(type==RESAMPLE_DOWN_FORCE_PCR_SLOW){
	play_mode=PLAY_MODE_SLOW;
	printk("[%s:%d] Audio to FORCE_PCR_SLOW\n", __FUNCTION__, __LINE__);
    }
    return size;
}
// --------------------------------------------------------------------------------
// define of tsync pcr module

static struct class_attribute tsync_pcr_class_attrs[] = {
    __ATTR(play_mode,  S_IRUGO | S_IWUSR | S_IWGRP, show_play_mode, NULL),
    __ATTR(tsync_pcr_discontinue_point, S_IRUGO | S_IWUSR, show_tsync_pcr_dispoint,  store_tsync_pcr_dispoint),
    __ATTR(audio_resample_type, S_IRUGO | S_IWUSR, NULL,  store_tsync_pcr_audio_resample_type),
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

    printk("[tsync_pcr_init]init success. \n");
    return (0);
}

static void __exit tsync_pcr_exit(void)
{
    class_unregister(&tsync_pcr_class);
    printk("[tsync_pcr_exit]exit success.   \n");
}


module_init(tsync_pcr_init);
module_exit(tsync_pcr_exit);

MODULE_DESCRIPTION("AMLOGIC time sync management driver of referrence by pcrscr");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("le yang <le.yang@amlogic.com>");
