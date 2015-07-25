#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/amlogic/amports/timestamp.h>
#include <linux/amlogic/amports/tsync.h>
#include <linux/amlogic/amports/ptsserv.h>
#ifdef ARC_700
#include <asm/arch/am_regs.h>
#else
#include <mach/am_regs.h>
#endif
#include "vdec_reg.h"
#include "amvdec.h"
#include "tsync_pcr.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
//TODO: for stream buffer register bit define only
#include "streambuf_reg.h"
#endif



#if !defined(CONFIG_PREEMPT)
#define CONFIG_AM_TIMESYNC_LOG
#endif

#ifdef CONFIG_AM_TIMESYNC_LOG
#define AMLOG
#define LOG_LEVEL_ERROR     0
#define LOG_LEVEL_ATTENTION 1
#define LOG_LEVEL_INFO      2
#define LOG_LEVEL_VAR       amlog_level_tsync
#define LOG_MASK_VAR        amlog_mask_tsync
#endif
#include <linux/amlogic/amlog.h>
MODULE_AMLOG(AMLOG_DEFAULT_LEVEL, 0, LOG_DEFAULT_LEVEL_DESC, LOG_DEFAULT_MASK_DESC);

//#define DEBUG
#define AVEVENT_FLAG_PARAM  0x01

//#define TSYNC_SLOW_SYNC

#define PCR_CHECK_INTERVAL  (HZ * 5)
#define PCR_DETECT_MARGIN_SHIFT_AUDIO_HI     7
#define PCR_DETECT_MARGIN_SHIFT_AUDIO_LO     7
#define PCR_DETECT_MARGIN_SHIFT_VIDEO_HI     4
#define PCR_DETECT_MARGIN_SHIFT_VIDEO_LO     4
#define PCR_MAINTAIN_MARGIN_SHIFT_AUDIO      4
#define PCR_MAINTAIN_MARGIN_SHIFT_VIDEO      1
#define PCR_RECOVER_PCR_ADJ 15

enum {
    PCR_SYNC_UNSET,
    PCR_SYNC_HI,
    PCR_SYNC_LO,
};

enum {
    PCR_TRIGGER_AUDIO,
    PCR_TRIGGER_VIDEO
};

typedef enum {
    TSYNC_STAT_PCRSCR_SETUP_NONE,
    TSYNC_STAT_PCRSCR_SETUP_VIDEO,
    TSYNC_STAT_PCRSCR_SETUP_AUDIO
} tsync_stat_t;

enum {
    TOGGLE_MODE_FIXED = 0,    // Fixed: use the Nominal M/N values
    TOGGLE_MODE_NORMAL_LOW,   // Toggle between the Nominal M/N values and the Low M/N values
    TOGGLE_MODE_NORMAL_HIGH,  // Toggle between the Nominal M/N values and the High M/N values
    TOGGLE_MODE_LOW_HIGH,     // Toggle between the Low M/N values and the High M/N Values
};

const static struct {
    const char *token;
    const u32 token_size;
    const avevent_t event;
    const u32 flag;
} avevent_token[] = {
    {"VIDEO_START", 11, VIDEO_START, AVEVENT_FLAG_PARAM},
    {"VIDEO_STOP",  10, VIDEO_STOP,  0},
    {"VIDEO_PAUSE", 11, VIDEO_PAUSE, AVEVENT_FLAG_PARAM},
    {"VIDEO_TSTAMP_DISCONTINUITY", 26, VIDEO_TSTAMP_DISCONTINUITY, AVEVENT_FLAG_PARAM},
    {"AUDIO_START", 11, AUDIO_START, AVEVENT_FLAG_PARAM},
    {"AUDIO_RESUME", 12, AUDIO_RESUME, 0},
    {"AUDIO_STOP",  10, AUDIO_STOP,  0},
    {"AUDIO_PAUSE", 11, AUDIO_PAUSE, 0},
    {"AUDIO_TSTAMP_DISCONTINUITY", 26, AUDIO_TSTAMP_DISCONTINUITY, AVEVENT_FLAG_PARAM},
    {"AUDIO_PRE_START",15, AUDIO_PRE_START, 0 },
};

const static char *tsync_mode_str[] = {
    "vmaster", "amaster", "pcrmaster"
};

static DEFINE_SPINLOCK(lock);
static tsync_mode_t tsync_mode = TSYNC_MODE_AMASTER;
static tsync_stat_t tsync_stat = TSYNC_STAT_PCRSCR_SETUP_NONE;
static int tsync_enable = 0;   //1;
static int apts_discontinue = 0;
static int vpts_discontinue = 0;
static int pts_discontinue = 0;
static u32 apts_discontinue_diff = 0;
static u32 vpts_discontinue_diff = 0;
static int tsync_abreak = 0;
static bool tsync_pcr_recover_enable = false;
static int pcr_sync_stat = PCR_SYNC_UNSET;
static int pcr_recover_trigger = 0;
static struct timer_list tsync_pcr_recover_timer;
static int tsync_trickmode = 0;
static int vpause_flag = 0;
static int apause_flag = 0;
static bool dobly_avsync_test = false;


/*
                  threshold_min              threshold_max
                         |                          |			
AMASTER<-------------->  |<-----DYNAMIC VMASTER---->|	VMASTER
static mode  | a dynamic  |            dynamic mode  |static mode 

static mode(S), Amster and Vmaster..
dynamic mode (D)  : discontinue....>min,< max,can switch to static mode,if diff is <min,and become to Sd mode if timerout.
sdynamic mode (A): dynamic mode become to static mode,because timer out, Don't do switch  befome timeout.

tsync_av_mode switch...
(AMASTER S)<-->(D VMASTER)<--> (S VMASTER)
(D VMASTER)--time out->(A AMASTER)--time out->((AMASTER S))
*/
unsigned  int tsync_av_threshold_min= AV_DISCONTINUE_THREDHOLD_MIN;
unsigned  int tsync_av_threshold_max = AV_DISCONTINUE_THREDHOLD_MAX;
#define TSYNC_STATE_S  ('S')
#define TSYNC_STATE_A ('A')
#define TSYNC_STATE_D  ('D')
static unsigned int tsync_av_mode=TSYNC_STATE_S; //S=1,A=2,D=3;
static unsigned int tsync_av_latest_switch_time_ms; //the time on latset switch
static unsigned int tsync_av_dynamic_duration_ms;//hold for dynamic mode;
static unsigned int tsync_av_dynamic_timeout_ms;//hold for dynamic mode;
static struct timer_list tsync_state_switch_timer;
#define jiffies_ms (jiffies*1000/HZ)


static unsigned int tsync_syncthresh = 1;
static int tsync_dec_reset_flag = 0;
static int tsync_dec_reset_video_start = 0;
static int tsync_automute_on = 0;
static int tsync_video_started = 0;

static int debug_pts_checkin = 0;
static int debug_pts_checkout = 0;
static int debug_vpts = 0;
static int debug_apts = 0;

#define M_HIGH_DIFF    2
#define M_LOW_DIFF     2
#define PLL_FACTOR   10000

#define LOW_TOGGLE_TIME           99
#define NORMAL_TOGGLE_TIME        499
#define HIGH_TOGGLE_TIME          99



#define PTS_CACHED_LO_NORMAL_TIME (90000)
#define PTS_CACHED_NORMAL_LO_TIME (45000)
#define PTS_CACHED_HI_NORMAL_TIME (135000)
#define PTS_CACHED_NORMAL_HI_TIME (180000)

#ifdef MODIFY_TIMESTAMP_INC_WITH_PLL
extern void set_timestamp_inc_factor(u32 factor);
#endif

#ifdef CALC_CACHED_TIME
extern int pts_cached_time(u8 type);
#endif

extern int get_vsync_pts_inc_mode(void);

static void tsync_pcr_recover_with_audio(void)
{
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON6
    u32 ab_level = READ_MPEG_REG(AIU_MEM_AIFIFO_LEVEL);
    u32 ab_size = READ_MPEG_REG(AIU_MEM_AIFIFO_END_PTR)
                  - READ_MPEG_REG(AIU_MEM_AIFIFO_START_PTR) + 8;
    u32 vb_level = READ_VREG(VLD_MEM_VIFIFO_LEVEL);
    u32 vb_size = READ_VREG(VLD_MEM_VIFIFO_END_PTR)
                  - READ_VREG(VLD_MEM_VIFIFO_START_PTR) + 8;

    if ((READ_MPEG_REG(AIU_MEM_I2S_CONTROL) &
         (MEM_CTRL_EMPTY_EN | MEM_CTRL_EMPTY_EN)) == 0) {
        return;
    }

    //printk("ab_size:%d ab_level:%d vb_size:%d vb_level:%d\n", ab_size, ab_level, vb_size, vb_level);

    //printk("vpts diff %d apts diff %d vlevel %d alevel %d\n", pts_cached_time(PTS_TYPE_VIDEO), pts_cached_time(PTS_TYPE_AUDIO), vb_level, ab_level);

    if ((unlikely(pcr_sync_stat != PCR_SYNC_LO)) &&
#ifndef CALC_CACHED_TIME
        ((ab_level < (ab_size >> PCR_DETECT_MARGIN_SHIFT_AUDIO_LO)) ||
         (vb_level < (vb_size >> PCR_DETECT_MARGIN_SHIFT_VIDEO_LO)))
#else
	(pts_cached_time(PTS_TYPE_VIDEO)<PTS_CACHED_NORMAL_LO_TIME) && (pts_cached_time(PTS_TYPE_AUDIO)<PTS_CACHED_NORMAL_LO_TIME)
#endif
		    ) {

        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) &
                       (~((1 << 31) | (TOGGLE_MODE_LOW_HIGH << 28))));
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) | (TOGGLE_MODE_NORMAL_LOW << 28));
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) | (1 << 31));

#ifdef MODIFY_TIMESTAMP_INC_WITH_PLL
	{
		u32 inc, M_nom, N_nom;

		M_nom = READ_MPEG_REG(HHI_AUD_PLL_CNTL) & 0x1ff;
		N_nom = (READ_MPEG_REG(HHI_AUD_PLL_CNTL) >> 9) & 0x1f;

		inc = (M_nom*(NORMAL_TOGGLE_TIME+1)+(M_nom-M_LOW_DIFF)*(LOW_TOGGLE_TIME+1))*PLL_FACTOR/((NORMAL_TOGGLE_TIME+LOW_TOGGLE_TIME+2)*M_nom);
		set_timestamp_inc_factor(inc);
		printk("pll low inc: %d factor: %d\n", inc, PLL_FACTOR);
	}
#endif

        pcr_sync_stat = PCR_SYNC_LO;
        printk("pcr_sync_stat = PCR_SYNC_LO\n");
        if (ab_level < (ab_size >> PCR_DETECT_MARGIN_SHIFT_AUDIO_LO)) {
            pcr_recover_trigger |= (1 << PCR_TRIGGER_AUDIO);
            printk("audio: 0x%x < 0x%x, vb_level 0x%x\n", ab_level, (ab_size >> PCR_DETECT_MARGIN_SHIFT_AUDIO_LO), vb_level);
        }
        if (vb_level < (vb_size >> PCR_DETECT_MARGIN_SHIFT_VIDEO_LO)) {
            pcr_recover_trigger |= (1 << PCR_TRIGGER_VIDEO);
            printk("video: 0x%x < 0x%x, ab_level 0x%x\n", vb_level, (vb_size >> PCR_DETECT_MARGIN_SHIFT_VIDEO_LO), ab_level);
        }
    } else if ((unlikely(pcr_sync_stat != PCR_SYNC_HI)) &&
#ifndef CALC_CACHED_TIME
		((((ab_level + (ab_size >> PCR_DETECT_MARGIN_SHIFT_AUDIO_HI)) > ab_size) ||
                ((vb_level + (vb_size >> PCR_DETECT_MARGIN_SHIFT_VIDEO_HI)) > vb_size)))
#else
		((pts_cached_time(PTS_TYPE_VIDEO)>=PTS_CACHED_NORMAL_HI_TIME) && (pts_cached_time(PTS_TYPE_AUDIO)>=PTS_CACHED_NORMAL_HI_TIME))
#endif
		) {

        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) &
                       (~((1 << 31) | (TOGGLE_MODE_LOW_HIGH << 28))));
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) | (TOGGLE_MODE_NORMAL_HIGH << 28));
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) | (1 << 31));
#ifdef MODIFY_TIMESTAMP_INC_WITH_PLL
	{
		u32 inc, M_nom, N_nom;

		M_nom = READ_MPEG_REG(HHI_AUD_PLL_CNTL) & 0x1ff;
		N_nom = (READ_MPEG_REG(HHI_AUD_PLL_CNTL) >> 9) & 0x1f;

		inc = (M_nom*(NORMAL_TOGGLE_TIME+1)+(M_nom+M_HIGH_DIFF)*(HIGH_TOGGLE_TIME+1))*PLL_FACTOR/((NORMAL_TOGGLE_TIME+HIGH_TOGGLE_TIME+2)*M_nom);
		set_timestamp_inc_factor(inc);
		printk("pll high inc: %d factor: %d\n", inc, PLL_FACTOR);
	}
#endif
        pcr_sync_stat = PCR_SYNC_HI;
        printk("pcr_sync_stat = PCR_SYNC_HI\n");
        if ((ab_level + (ab_size >> PCR_DETECT_MARGIN_SHIFT_AUDIO_HI)) > ab_size) {
            pcr_recover_trigger |= (1 << PCR_TRIGGER_AUDIO);
            printk("audio: 0x%x+0x%x > 0x%x, vb_level 0x%x\n", ab_level, (ab_size >> PCR_DETECT_MARGIN_SHIFT_AUDIO_HI), ab_size, vb_level);
        }
        if ((vb_level + (vb_size >> PCR_DETECT_MARGIN_SHIFT_VIDEO_HI)) > vb_size) {
            pcr_recover_trigger |= (1 << PCR_TRIGGER_VIDEO);
            printk("video: 0x%x+0x%x > 0x%x, ab_level 0x%x\n", vb_level, (vb_size >> PCR_DETECT_MARGIN_SHIFT_VIDEO_HI), vb_size, ab_level);
        }
    } else if (
    		((pcr_sync_stat == PCR_SYNC_LO) &&
#ifndef CALC_CACHED_TIME
                (((!(pcr_recover_trigger & (1 << PCR_TRIGGER_AUDIO))) || (ab_level > (ab_size >> PCR_MAINTAIN_MARGIN_SHIFT_AUDIO)))
                &&
                ((!(pcr_recover_trigger & (1 << PCR_TRIGGER_VIDEO))) || ((vb_level + (vb_size >> PCR_MAINTAIN_MARGIN_SHIFT_VIDEO)) > vb_size)))
#else
		((pts_cached_time(PTS_TYPE_VIDEO)>=PTS_CACHED_LO_NORMAL_TIME) || (pts_cached_time(PTS_TYPE_AUDIO)>=PTS_CACHED_LO_NORMAL_TIME))
#endif
		)
               ||
               ((pcr_sync_stat == PCR_SYNC_HI) &&
#ifndef CALC_CACHED_TIME
                ((!(pcr_recover_trigger & (1 << PCR_TRIGGER_AUDIO))) || ((ab_level + (ab_size >> PCR_MAINTAIN_MARGIN_SHIFT_AUDIO)) < ab_size))
                &&
                ((!(pcr_recover_trigger & (1 << PCR_TRIGGER_VIDEO))) || (vb_level < (vb_size >> PCR_MAINTAIN_MARGIN_SHIFT_VIDEO)))
#else
		((pts_cached_time(PTS_TYPE_VIDEO)<PTS_CACHED_HI_NORMAL_TIME) || (pts_cached_time(PTS_TYPE_AUDIO)<PTS_CACHED_HI_NORMAL_TIME))
#endif
		)) {

        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) &
                       (~((1 << 31) | (TOGGLE_MODE_LOW_HIGH << 28))));
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) | (TOGGLE_MODE_FIXED << 28));
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) | (1 << 31));

#ifdef MODIFY_TIMESTAMP_INC_WITH_PLL
	{
		set_timestamp_inc_factor(PLL_FACTOR);
		printk("pll normal inc:%d\n", PLL_FACTOR);
	}
#endif

        pcr_sync_stat = PCR_SYNC_UNSET;
        pcr_recover_trigger = 0;
        printk("pcr_sync_stat = PCR_SYNC_UNSET ab_level: 0x%x, vb_level: 0x%x\n", ab_level, vb_level);
    }
#endif
}

static void tsync_pcr_recover_with_video(void)
{
    u32 vb_level = READ_VREG(VLD_MEM_VIFIFO_LEVEL);
    u32 vb_size = READ_VREG(VLD_MEM_VIFIFO_END_PTR)
                  - READ_VREG(VLD_MEM_VIFIFO_START_PTR) + 8;

    if (vb_level < (vb_size >> PCR_DETECT_MARGIN_SHIFT_VIDEO_LO)) {
        timestamp_pcrscr_set_adj(-PCR_RECOVER_PCR_ADJ);
        printk(" timestamp_pcrscr_set_adj(-%d);\n", PCR_RECOVER_PCR_ADJ);
    } else if ((vb_level + (vb_size >> PCR_DETECT_MARGIN_SHIFT_VIDEO_HI)) > vb_size) {
        timestamp_pcrscr_set_adj(PCR_RECOVER_PCR_ADJ);
        printk(" timestamp_pcrscr_set_adj(%d);\n", PCR_RECOVER_PCR_ADJ);
    } else if ((vb_level > (vb_size >> PCR_MAINTAIN_MARGIN_SHIFT_VIDEO)) ||
               (vb_level < (vb_size - (vb_size >> PCR_MAINTAIN_MARGIN_SHIFT_VIDEO)))) {
        timestamp_pcrscr_set_adj(0);
    }
}

static bool tsync_pcr_recover_use_video(void)
{
    /* This is just a hacking to use audio output enable
     * as the flag to check if this is a video only playback.
     * Such processing can not handle an audio output with a
     * mixer so audio playback has no direct relationship with
     * applications. TODO.
     */
    return ((READ_MPEG_REG(AIU_MEM_I2S_CONTROL) &
             (MEM_CTRL_EMPTY_EN | MEM_CTRL_EMPTY_EN)) == 0);
}

static void tsync_pcr_recover_timer_real(void)
{
    ulong flags;

    spin_lock_irqsave(&lock, flags);

    if (tsync_pcr_recover_enable) {
        if (tsync_pcr_recover_use_video()) {
            tsync_pcr_recover_with_video();
        } else {
            timestamp_pcrscr_set_adj(0);
            tsync_pcr_recover_with_audio();
        }
    }

    spin_unlock_irqrestore(&lock, flags);
}

static void tsync_pcr_recover_timer_func(unsigned long arg)
{
    tsync_pcr_recover_timer_real();
    tsync_pcr_recover_timer.expires = jiffies + PCR_CHECK_INTERVAL;
    add_timer(&tsync_pcr_recover_timer);
}

/*
mode:
mode='V': diff_pts=|vpts-pcrscr|,jump_pts=vpts jump, video discontinue
mode='A': diff_pts=|apts-pcrscr|,jump_pts=apts jump, audio discontinue
mode='T': diff_pts=|vpts-apts|,timeout mode switch,
*/

static int tsync_mode_switch(int mode,unsigned long diff_pts,int jump_pts)
{
	int debugcnt=0;
	int old_tsync_mode=tsync_mode;
	int old_tsync_av_mode=tsync_av_mode;
	char VA[]="VA--";
       unsigned int oldtimeout=tsync_av_dynamic_timeout_ms;
	
        if(tsync_mode == TSYNC_MODE_PCRMASTER){
                printk("[tsync_mode_switch]tsync_mode is pcr master, do nothing \n");
                return 0;
        }

	printk("%c-discontinue,pcr=%d,vpts=%d,apts=%d,diff_pts=%lu,jump_Pts=%d\n",mode,timestamp_pcrscr_get(),timestamp_vpts_get(),timestamp_apts_get(),diff_pts,jump_pts);
	if (!tsync_enable) {
        if(tsync_mode != TSYNC_MODE_VMASTER)
			tsync_mode = TSYNC_MODE_VMASTER;
        tsync_av_mode=TSYNC_STATE_S;
		tsync_av_dynamic_duration_ms=0;
        printk("tsync_enable [%d] \n",tsync_enable);
		return 0;
    }
	if(mode=='T'){/*D/A--> ...*/
		if(tsync_av_mode==TSYNC_STATE_D){
			debugcnt|=1<<1;
			tsync_av_mode=TSYNC_STATE_A;
			tsync_mode = TSYNC_MODE_AMASTER;
			tsync_av_latest_switch_time_ms=jiffies_ms;
			tsync_av_dynamic_duration_ms=200+diff_pts*1000/TIME_UNIT90K;
			if(tsync_av_dynamic_duration_ms>5*1000)
				tsync_av_dynamic_duration_ms=5*1000;
		}else if(tsync_av_mode==TSYNC_STATE_A){
			debugcnt|=1<<2;
			tsync_av_mode=TSYNC_STATE_S;
			tsync_mode = TSYNC_MODE_AMASTER;
			tsync_av_latest_switch_time_ms=jiffies_ms;
			tsync_av_dynamic_duration_ms=0;
		}else{
			///
		}
		if(tsync_mode!=old_tsync_mode || tsync_av_mode!=old_tsync_av_mode)
			printk("mode changes:tsync_mode:%c->%c,state:%c->%c,debugcnt=0x%x,diff_pts=%lu\n",
			VA[old_tsync_mode],VA[tsync_mode],old_tsync_av_mode,tsync_av_mode,debugcnt,diff_pts);
		return 0;
	}


	if(diff_pts<tsync_av_threshold_min){/*->min*/
			debugcnt|=1<<1;
			tsync_av_mode=TSYNC_STATE_S;
			tsync_mode = TSYNC_MODE_AMASTER;
			tsync_av_latest_switch_time_ms=jiffies_ms;
			tsync_av_dynamic_duration_ms=0;
	}else if(diff_pts<=tsync_av_threshold_max){/*min<-->max*/
		if(tsync_av_mode==TSYNC_STATE_S){
			debugcnt|=1<<2;
			tsync_av_mode=TSYNC_STATE_D;
			tsync_mode = TSYNC_MODE_VMASTER;
			tsync_av_latest_switch_time_ms=jiffies_ms;
			tsync_av_dynamic_duration_ms=20*1000;
		}else if(tsync_av_mode==TSYNC_STATE_D){/*new discontinue,continue wait....*/
			debugcnt|=1<<7;
                        tsync_av_mode=TSYNC_STATE_D;
                        tsync_mode = TSYNC_MODE_VMASTER;
                        tsync_av_latest_switch_time_ms=jiffies_ms;
                        tsync_av_dynamic_duration_ms=20*1000;
		}
	}else if(diff_pts>=tsync_av_threshold_max){/*max<--*/
		if(tsync_av_mode==TSYNC_STATE_D ){
			debugcnt|=1<<3;
			tsync_av_mode=TSYNC_STATE_S;
			tsync_mode = TSYNC_MODE_VMASTER;
			tsync_av_latest_switch_time_ms=jiffies_ms;
			tsync_av_dynamic_duration_ms=0;
		}else if(tsync_mode != TSYNC_MODE_VMASTER){
			debugcnt|=1<<4;
			tsync_av_mode=TSYNC_STATE_S;
			tsync_mode = TSYNC_MODE_VMASTER;
			tsync_av_latest_switch_time_ms=jiffies_ms;
			tsync_av_dynamic_duration_ms=0;
		}
	}else{
		debugcnt|=1<<16;
	}


	
	if(oldtimeout!=tsync_av_latest_switch_time_ms+tsync_av_dynamic_duration_ms){/*duration changed,update new timeout.*/
			tsync_av_dynamic_timeout_ms=tsync_av_latest_switch_time_ms+tsync_av_dynamic_duration_ms;
	}
	printk("discontinue-tsync_mode:%c->%c,state:%c->%c,debugcnt=0x%x,diff_pts=%lu,tsync_mode=%d\n",
                	VA[old_tsync_mode],VA[tsync_mode],old_tsync_av_mode,tsync_av_mode,debugcnt,diff_pts,tsync_mode);	
	return 0;
}
static void tsync_state_switch_timer_fun(unsigned long arg)
{
	if(!vpause_flag && !apause_flag){
		if(tsync_av_mode==TSYNC_STATE_D || tsync_av_mode==TSYNC_STATE_A){
			if(tsync_av_dynamic_timeout_ms<jiffies_ms)
			{
				//to be amaster?
				tsync_mode_switch('T',abs(timestamp_vpts_get()-timestamp_apts_get()),0);
				if(tsync_mode == TSYNC_MODE_AMASTER && abs(timestamp_apts_get()-timestamp_pcrscr_get())>(TIME_UNIT90K*50/1000))
					timestamp_pcrscr_set(timestamp_apts_get());
			}	
		}
	}else{
		//video&audio paused,changed the timeout time to latter.
		tsync_av_dynamic_timeout_ms=jiffies_ms+tsync_av_dynamic_duration_ms;
	}
 	tsync_state_switch_timer.expires = jiffies + 20;
	add_timer(&tsync_state_switch_timer);
}
void tsync_mode_reinit(void)
{
	tsync_av_mode=TSYNC_STATE_S;
    tsync_av_dynamic_duration_ms=0;
    return ;
}
EXPORT_SYMBOL(tsync_mode_reinit);
void tsync_avevent_locked(avevent_t event, u32 param)
{
    u32 t;

    if(tsync_mode == TSYNC_MODE_PCRMASTER){
    	amlog_level(LOG_LEVEL_INFO,"[tsync_avevent_locked]PCR MASTER to use tsync pcr cmd deal ");
	tsync_pcr_avevent_locked(event,param);
	return;
    }

    switch (event) {
    case VIDEO_START:
        tsync_video_started = 1;
        /*set tsync mode to vmaster to avoid video block caused by avpts-diff too much
          threshold 120s is an arbitrary value*/  
        t = abs(timestamp_apts_get()-timestamp_vpts_get())/TIME_UNIT90K;
        if (tsync_enable && !get_vsync_pts_inc_mode() && t<120) {
            tsync_mode = TSYNC_MODE_AMASTER;
        } else {
            tsync_mode = TSYNC_MODE_VMASTER;
            if (get_vsync_pts_inc_mode()) {
                tsync_stat = TSYNC_STAT_PCRSCR_SETUP_NONE;
            }
        }

        if (tsync_dec_reset_flag) {
            tsync_dec_reset_video_start = 1;
        }

#ifndef TSYNC_SLOW_SYNC
        if (tsync_stat == TSYNC_STAT_PCRSCR_SETUP_NONE)
#endif
        {
#ifndef TSYNC_SLOW_SYNC
            if (tsync_syncthresh && (tsync_mode == TSYNC_MODE_AMASTER)) {
                timestamp_pcrscr_set(param - VIDEO_HOLD_THRESHOLD);
            } else {
                timestamp_pcrscr_set(param);
            }
#else
            timestamp_pcrscr_set(param);
#endif

            tsync_stat = TSYNC_STAT_PCRSCR_SETUP_VIDEO;
            amlog_level(LOG_LEVEL_INFO, "vpts to scr, apts = 0x%x, vpts = 0x%x\n",
                        timestamp_apts_get(),
                        timestamp_vpts_get());
        }

        if (tsync_stat == TSYNC_STAT_PCRSCR_SETUP_AUDIO) {
            t = timestamp_pcrscr_get();
            if (abs(param - t) > tsync_av_threshold_max) {
                /* if this happens, then play */
                tsync_stat = TSYNC_STAT_PCRSCR_SETUP_VIDEO;
                timestamp_pcrscr_set(param);
            }
        }
        if (/*tsync_mode == TSYNC_MODE_VMASTER && */!vpause_flag) {
            timestamp_pcrscr_enable(1);
        }

        if (!timestamp_firstvpts_get() && param) {
            timestamp_firstvpts_set(param);
        }    
        break;

    case VIDEO_STOP:
        tsync_stat = TSYNC_STAT_PCRSCR_SETUP_NONE;
        timestamp_vpts_set(0);
        timestamp_pcrscr_enable(0);
        timestamp_firstvpts_set(0);
        tsync_video_started = 0;
        break;

        /* Note:
         * Video and audio PTS discontinue happens typically with a loopback playback,
         * with same bit stream play in loop and PTS wrap back from start point.
         * When VIDEO_TSTAMP_DISCONTINUITY happens early, PCRSCR is set immedately to
         * make video still keep running in VMATSER mode. This mode is restored to
         * AMASTER mode when AUDIO_TSTAMP_DISCONTINUITY reports, or apts is close to
         * scr later.
         * When AUDIO_TSTAMP_DISCONTINUITY happens early, VMASTER mode is set to make
         * video still keep running w/o setting PCRSCR. This mode is restored to
         * AMASTER mode when VIDEO_TSTAMP_DISCONTINUITY reports, and scr is restored
         * along with new video time stamp also.
         */
    case VIDEO_TSTAMP_DISCONTINUITY:
	{
		unsigned oldpts=timestamp_vpts_get();
		int oldmod=tsync_mode;
		if(tsync_mode == TSYNC_MODE_VMASTER)
        		t = timestamp_apts_get();
		else
			t = timestamp_pcrscr_get();
        	//amlog_level(LOG_LEVEL_ATTENTION, "VIDEO_TSTAMP_DISCONTINUITY, 0x%x, 0x%x\n", t, param);
		if((abs(param-oldpts)>tsync_av_threshold_min) && (!get_vsync_pts_inc_mode())){
			vpts_discontinue=1;
			vpts_discontinue_diff = abs(param-t);
			tsync_mode_switch('V',abs(param - t),param-oldpts);
		}
		timestamp_vpts_set(param);
		if(tsync_mode == TSYNC_MODE_VMASTER){
			timestamp_pcrscr_set(param);	
		}else if(tsync_mode!=oldmod && tsync_mode == TSYNC_MODE_AMASTER){
			timestamp_pcrscr_set(timestamp_apts_get());
		}
	}
        break;

    case AUDIO_TSTAMP_DISCONTINUITY:
	{
		unsigned oldpts=timestamp_apts_get();
		int oldmod=tsync_mode;
        	amlog_level(LOG_LEVEL_ATTENTION, "audio discontinue, reset apts, 0x%x\n", param);	
		 		timestamp_apts_set(param);	
        	if (!tsync_enable) {
			timestamp_apts_set(param);
            		break;
        	}
		if(tsync_mode == TSYNC_MODE_AMASTER)
                        t = timestamp_vpts_get();
                else
                        t = timestamp_pcrscr_get();
				
        	amlog_level(LOG_LEVEL_ATTENTION, "AUDIO_TSTAMP_DISCONTINUITY, 0x%x, 0x%x\n", t, param);
		if((abs(param-oldpts)>tsync_av_threshold_min) && (!get_vsync_pts_inc_mode())){
			apts_discontinue=1;
			apts_discontinue_diff = abs(param-t);
			tsync_mode_switch('A',abs(param - t),param-oldpts);
		}
		timestamp_apts_set(param);
		if( tsync_mode == TSYNC_MODE_AMASTER){
			timestamp_pcrscr_set(param);		
		}else if(tsync_mode!=oldmod && tsync_mode == TSYNC_MODE_VMASTER){
                        timestamp_pcrscr_set(timestamp_vpts_get());
                }
	}
        break;

    case AUDIO_PRE_START:
        timestamp_apts_start(0);
        break;

    case AUDIO_START:		
        //reset discontinue var
        tsync_set_sync_adiscont(0);
        tsync_set_sync_adiscont_diff(0);
        tsync_set_sync_vdiscont(0);
        tsync_set_sync_vdiscont_diff(0);
        
        timestamp_apts_set(param);

		amlog_level(LOG_LEVEL_INFO, "audio start, reset apts = 0x%x\n", param);

        timestamp_apts_enable(1);
		 
        timestamp_apts_start(1);

        if (!tsync_enable) {
            break;
        }

        t = timestamp_pcrscr_get();

        amlog_level(LOG_LEVEL_INFO, "[%s]param %d, t %d, tsync_abreak %d\n",
                    __FUNCTION__, param, t, tsync_abreak);

        if (tsync_abreak && (abs(param - t) > TIME_UNIT90K / 10)) { // 100ms, then wait to match
            break;
        }

        tsync_abreak = 0;
        if (tsync_dec_reset_flag) { // after reset, video should be played first
            int vpts = timestamp_vpts_get();
            if ((param < vpts) || (!tsync_dec_reset_video_start)) {
                timestamp_pcrscr_set(param);
            } else {
                timestamp_pcrscr_set(vpts);
            }
            tsync_dec_reset_flag = 0;
            tsync_dec_reset_video_start = 0;
        } else if(tsync_mode == TSYNC_MODE_AMASTER)
        {
            timestamp_pcrscr_set(param);
        }
       
        tsync_stat = TSYNC_STAT_PCRSCR_SETUP_AUDIO;

        amlog_level(LOG_LEVEL_INFO, "apts reset scr = 0x%x\n", param);

        timestamp_pcrscr_enable(1);
        apause_flag = 0;
        break;

    case AUDIO_RESUME:
	timestamp_apts_enable(1);
	apause_flag = 0;	
        if (!tsync_enable) {
            break;
        }
        timestamp_pcrscr_enable(1);
        
        break;

    case AUDIO_STOP:
		timestamp_apts_enable(0);
		timestamp_apts_set(-1);
        tsync_abreak = 0;
        if (tsync_trickmode) {
            tsync_stat = TSYNC_STAT_PCRSCR_SETUP_VIDEO;
        } else {
            tsync_stat = TSYNC_STAT_PCRSCR_SETUP_NONE;
        }
        apause_flag = 0;
        timestamp_apts_start(0);
       
        break;

    case AUDIO_PAUSE:
        apause_flag = 1;
	timestamp_apts_enable(0);
		
        if (!tsync_enable) {
            break;
        }

        timestamp_pcrscr_enable(0);
        break;

    case VIDEO_PAUSE:
        if (param == 1) {
            vpause_flag = 1;
        } else {
            vpause_flag = 0;
        }
		if(param == 1){
        	timestamp_pcrscr_enable(0);
			amlog_level(LOG_LEVEL_INFO, "video pause!\n");
		}else{
		       if ((!apause_flag) || (!tsync_enable)) {
			timestamp_pcrscr_enable(1);
			amlog_level(LOG_LEVEL_INFO, "video resume\n");
                      }
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
        if (vpause_flag)
            amvdev_pause();
        else
            amvdev_resume();
        break;
    default:
        break;
    }
}
EXPORT_SYMBOL(tsync_avevent_locked);

void tsync_avevent(avevent_t event, u32 param)
{
    ulong flags;
    ulong fiq_flag;
    amlog_level(LOG_LEVEL_INFO, "[%s]event:%d, param %d\n",
                __FUNCTION__, event, param);
    spin_lock_irqsave(&lock, flags);

    raw_local_save_flags(fiq_flag);

    local_fiq_disable();

    tsync_avevent_locked(event, param);

    raw_local_irq_restore(fiq_flag);

    spin_unlock_irqrestore(&lock, flags);
}
EXPORT_SYMBOL(tsync_avevent);

void tsync_audio_break(int audio_break)
{
    tsync_abreak = audio_break;
    return;
}
EXPORT_SYMBOL(tsync_audio_break);

void tsync_trick_mode(int trick_mode)
{
    tsync_trickmode = trick_mode;
    return;
}
EXPORT_SYMBOL(tsync_trick_mode);

void tsync_set_avthresh(unsigned int av_thresh)
{
    //tsync_av_thresh = av_thresh;
    return;
}
EXPORT_SYMBOL(tsync_set_avthresh);

void tsync_set_syncthresh(unsigned int sync_thresh)
{
    tsync_syncthresh = sync_thresh;
    return;
}
EXPORT_SYMBOL(tsync_set_syncthresh);

void tsync_set_dec_reset(void)
{
    tsync_dec_reset_flag = 1;
}
EXPORT_SYMBOL(tsync_set_dec_reset);

void tsync_set_enable(int enable)
{
    tsync_enable = enable;
    tsync_av_mode=TSYNC_STATE_S;
}
EXPORT_SYMBOL(tsync_set_enable);

int tsync_get_sync_adiscont(void)
{	
    return apts_discontinue;
}
EXPORT_SYMBOL(tsync_get_sync_adiscont);

int tsync_get_sync_vdiscont(void)
{	
    return vpts_discontinue;
}
EXPORT_SYMBOL(tsync_get_sync_vdiscont);
u32 tsync_get_sync_adiscont_diff(void)
{	
    return apts_discontinue_diff;
}
EXPORT_SYMBOL(tsync_get_sync_adiscont_diff);

u32 tsync_get_sync_vdiscont_diff(void)
{	
    return vpts_discontinue_diff;
}
EXPORT_SYMBOL(tsync_get_sync_vdiscont_diff);
void tsync_set_sync_adiscont_diff(u32 discontinue_diff)
{
	apts_discontinue_diff = discontinue_diff;
}
EXPORT_SYMBOL(tsync_set_sync_adiscont_diff);
void tsync_set_sync_vdiscont_diff(u32 discontinue_diff)
{
	vpts_discontinue_diff = discontinue_diff;
}
EXPORT_SYMBOL(tsync_set_sync_vdiscont_diff);

void tsync_set_sync_adiscont(int syncdiscont)
{
    apts_discontinue = syncdiscont;
}
EXPORT_SYMBOL(tsync_set_sync_adiscont);

void tsync_set_sync_vdiscont(int syncdiscont)
{
    vpts_discontinue = syncdiscont;
}
EXPORT_SYMBOL(tsync_set_sync_vdiscont);

void tsync_set_automute_on(int automute_on)
{
    tsync_automute_on = automute_on;
}
EXPORT_SYMBOL(tsync_set_automute_on);

int tsync_set_apts(unsigned pts)
{
    unsigned  t;
    //ssize_t r;
    unsigned oldpts=timestamp_apts_get();
    int oldmod=tsync_mode;
    if (tsync_abreak) {
        tsync_abreak = 0;
    }
    if (!tsync_enable) {
	timestamp_apts_set(pts);
        return 0;
    }
    if(tsync_mode == TSYNC_MODE_AMASTER)
    	t = timestamp_vpts_get();
    else 
	t = timestamp_pcrscr_get();
    if((abs(oldpts-pts)>tsync_av_threshold_min) && (!get_vsync_pts_inc_mode())){ //is discontinue 
        apts_discontinue=1;
        tsync_mode_switch('A',abs(pts - t),pts-oldpts);/*if in VMASTER ,just wait */
    }
    timestamp_apts_set(pts); 

    if (get_vsync_pts_inc_mode() && (tsync_mode != TSYNC_MODE_VMASTER)) 
    {
        tsync_mode = TSYNC_MODE_VMASTER;
    }

    if(tsync_mode == TSYNC_MODE_AMASTER)
        t = timestamp_pcrscr_get();

    if( tsync_mode == TSYNC_MODE_AMASTER ) {
        if(dobly_avsync_test){//special used for Dobly Certification AVSync test
            if (get_vsync_pts_inc_mode()
                && (((int)(timestamp_apts_get()-t)>(int)60*TIME_UNIT90K/1000) || (int)(t - timestamp_apts_get())>(int)100*TIME_UNIT90K/1000)){
                printk("[%d:avsync_test]reset apts:0x%x-->0x%x, pcr 0x%x, diff %d\n",__LINE__,oldpts,pts,t,pts-t);
                timestamp_pcrscr_set(pts);
            } else if ((!get_vsync_pts_inc_mode())&&  \
        	((int)(timestamp_apts_get()-t)> 30*TIME_UNIT90K/1000 || (int)(t-timestamp_apts_get())> 80*TIME_UNIT90K/1000))
        	/* && (abs(timestamp_apts_get()-t)> 100*TIME_UNIT90K/1000)) */ {
                //printk("[%d:avsync_test]reset apts:0x%x-->0x%x, pcr 0x%x, diff %d\n",__LINE__,oldpts,pts,t,pts-t);
                timestamp_pcrscr_set(pts);
            }
        }else{
            if (get_vsync_pts_inc_mode()
              && (((int)(timestamp_apts_get()-t)>(int)100*TIME_UNIT90K/1000) || (int)(t - timestamp_apts_get())>(int)500*TIME_UNIT90K/1000)){
                printk("[%d]reset apts:0x%x-->0x%x, pcr 0x%x, diff %d\n",__LINE__,oldpts,pts,t,pts-t);
                timestamp_pcrscr_set(pts);
            } else if ((!get_vsync_pts_inc_mode()) && (abs(timestamp_apts_get()-t)>100*TIME_UNIT90K/1000)) {
                printk("[%d]reset apts:0x%x-->0x%x, pcr 0x%x, diff %d\n",__LINE__,oldpts,pts,t,pts-t);
                timestamp_pcrscr_set(pts);
            }        
        }
    }else if(oldmod!=tsync_mode && tsync_mode==TSYNC_MODE_VMASTER){
	timestamp_pcrscr_set(timestamp_vpts_get());
    }
    return 0;
}
EXPORT_SYMBOL(tsync_set_apts);

/*********************************************************/

static ssize_t show_pcr_recover(struct class *class,
                                struct class_attribute *attr,
                                char *buf)
{
    return sprintf(buf, "%s %s\n", ((tsync_pcr_recover_enable) ? "on" : "off"), ((pcr_sync_stat == PCR_SYNC_UNSET) ? ("UNSET") : ((pcr_sync_stat == PCR_SYNC_LO) ? "LO" : "HI")));
}

void tsync_pcr_recover(void)
{
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON6
	unsigned long M_nom, N_nom;
	
	if (tsync_pcr_recover_enable) {

        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_LOW_TCNT,  LOW_TOGGLE_TIME);       // Set low toggle time (oscillator clock cycles)
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_NOM_TCNT,  NORMAL_TOGGLE_TIME);    // Set nominal toggle time (oscillator clock cycles)
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_HIGH_TCNT, HIGH_TOGGLE_TIME);      // Set high toggle time (oscillator clock cycles)

        M_nom   = READ_MPEG_REG(HHI_AUD_PLL_CNTL) & 0x1ff;
        N_nom   = (READ_MPEG_REG(HHI_AUD_PLL_CNTL) >> 9) & 0x1f;

        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0, (0 << 31)      |
                       (TOGGLE_MODE_FIXED << 28)  |   // Toggle mode
                       (N_nom << 23)              |   // N high value (not used)
                       ((M_nom + M_HIGH_DIFF) << 14)          | // M high value
                       (N_nom << 9)               |   // N low value (not used)
                       ((M_nom - M_LOW_DIFF) << 0));  // M low value
        pcr_sync_stat = PCR_SYNC_UNSET;
        pcr_recover_trigger = 0;

	tsync_pcr_recover_timer_real();
	}
#endif
}


EXPORT_SYMBOL(tsync_pcr_recover);

int tsync_get_mode(void)
{
    return tsync_mode;
}
EXPORT_SYMBOL(tsync_get_mode);

int tsync_get_debug_pts_checkin(void)
{
    return debug_pts_checkin;
}
EXPORT_SYMBOL(tsync_get_debug_pts_checkin);

int tsync_get_debug_pts_checkout(void)
{
    return debug_pts_checkout;
}
EXPORT_SYMBOL(tsync_get_debug_pts_checkout);

int tsync_get_debug_vpts(void)
{
    return debug_vpts;
}
EXPORT_SYMBOL(tsync_get_debug_vpts);

int tsync_get_debug_apts(void)
{
    return debug_apts;
}
EXPORT_SYMBOL(tsync_get_debug_apts);

int tsync_get_av_threshold_min(void)
{ 
    return tsync_av_threshold_min;
}
EXPORT_SYMBOL(tsync_get_av_threshold_min);

int tsync_get_av_threshold_max(void)
{ 
    return tsync_av_threshold_max;
}
EXPORT_SYMBOL(tsync_get_av_threshold_max);
int tsync_set_av_threshold_min(int min)
{
     
    return tsync_av_threshold_min=min;
}
EXPORT_SYMBOL(tsync_set_av_threshold_min);

int tsync_set_av_threshold_max(int max)
{
 
    return tsync_av_threshold_max=max;
}
EXPORT_SYMBOL(tsync_set_av_threshold_max);

int tsync_get_vpause_flag(void)
{
    return vpause_flag;
}
EXPORT_SYMBOL(tsync_get_vpause_flag);

int tsync_set_vpause_flag(int mode)
{
    return vpause_flag=mode;
}
EXPORT_SYMBOL(tsync_set_vpause_flag);

static ssize_t store_pcr_recover(struct class *class,
                                 struct class_attribute *attr,
                                 const char *buf,
                                 size_t size)
{
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON6
    unsigned val;
    unsigned long M_nom, N_nom;
    ssize_t r;

    r = sscanf(buf, "%d", &val);
    if (r != 1) {
        return -EINVAL;
    }

    if (tsync_pcr_recover_enable == (val != 0)) {
        return size;
    }

    tsync_pcr_recover_enable = (val != 0);

    if (tsync_pcr_recover_enable) {

        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_LOW_TCNT,  LOW_TOGGLE_TIME);       // Set low toggle time (oscillator clock cycles)
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_NOM_TCNT,  NORMAL_TOGGLE_TIME);    // Set nominal toggle time (oscillator clock cycles)
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_HIGH_TCNT, HIGH_TOGGLE_TIME);      // Set high toggle time (oscillator clock cycles)

        M_nom   = READ_MPEG_REG(HHI_AUD_PLL_CNTL) & 0x1ff;
        N_nom   = (READ_MPEG_REG(HHI_AUD_PLL_CNTL) >> 9) & 0x1f;

        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0, (0 << 31)      |
                       (TOGGLE_MODE_FIXED << 28)  |   // Toggle mode
                       (N_nom << 23)              |   // N high value (not used)
                       ((M_nom + M_HIGH_DIFF) << 14)          | // M high value
                       (N_nom << 9)               |   // N low value (not used)
                       ((M_nom - M_LOW_DIFF) << 0));  // M low value
        pcr_sync_stat = PCR_SYNC_UNSET;
        pcr_recover_trigger = 0;
	
	tsync_pcr_recover_timer_real();

    } else {
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) &
                       (~((1 << 31) | (TOGGLE_MODE_LOW_HIGH << 28))));
        WRITE_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0,  READ_MPEG_REG(HHI_AUD_PLL_MOD_CNTL0) | (TOGGLE_MODE_FIXED << 28));
        pcr_sync_stat = PCR_SYNC_UNSET;
        pcr_recover_trigger = 0;
    }
#endif

    return size;
}

static ssize_t show_vpts(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "0x%x\n", timestamp_vpts_get());
}

static ssize_t store_vpts(struct class *class,
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

    timestamp_vpts_set(pts);
    return size;
}

static ssize_t show_apts(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "0x%x\n", timestamp_apts_get());
}

static ssize_t store_apts(struct class *class,
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

    tsync_set_apts(pts);

    return size;
}

static ssize_t dobly_show_sync(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
	return sprintf(buf, "avsync:cur value is %d\n  0:no set(default)\n  1:avsync test\n",
		dobly_avsync_test ? 1:0);
}

static ssize_t dobly_store_sync(struct class *class,
                          struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
    unsigned value;
    ssize_t r;

    r = sscanf(buf, "%u", &value);
    if (r != 1) {
        return -EINVAL;
    }

    if(value == 1){
        dobly_avsync_test = true;
    }else{
        dobly_avsync_test = false;
    }

    return size;
}
static ssize_t show_pcrscr(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
    return sprintf(buf, "0x%x\n", timestamp_pcrscr_get());
}

static ssize_t store_pcrscr(struct class *class,
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

    timestamp_pcrscr_set(pts);

    return size;
}

static ssize_t store_event(struct class *class,
                           struct class_attribute *attr,
                           const char *buf,
                           size_t size)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(avevent_token); i++) {
        if (strncmp(avevent_token[i].token, buf, avevent_token[i].token_size) == 0) {
            if (avevent_token[i].flag & AVEVENT_FLAG_PARAM) {
                char *param_str = strchr(buf, ':');

                if (!param_str) {
                    return -EINVAL;
                }

                tsync_avevent(avevent_token[i].event,
                              simple_strtoul(param_str + 1, NULL, 16));
            } else {
                tsync_avevent(avevent_token[i].event, 0);
            }

            return size;
        }
    }

    return -EINVAL;
}

static ssize_t show_mode(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    if (tsync_mode <= TSYNC_MODE_PCRMASTER) {
        return sprintf(buf, "%d: %s\n", tsync_mode, tsync_mode_str[tsync_mode]);
    }

    return sprintf(buf, "invalid mode");
}

static ssize_t store_mode(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned mode;
    ssize_t r;

    r = sscanf(buf, "%d", &mode);
    if ((r != 1)) {
        return -EINVAL;
    }

    if(mode == TSYNC_MODE_PCRMASTER)
    	tsync_mode = TSYNC_MODE_PCRMASTER;
    else if(mode == TSYNC_MODE_VMASTER)
    	tsync_mode=TSYNC_MODE_VMASTER;
    else
    	tsync_mode=TSYNC_MODE_AMASTER;
    
    printk("[%s]tsync_mode=%d, buf=%s\n",__func__,tsync_mode,buf);
    return size;
}

static ssize_t show_enable(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
    if (tsync_enable) {
        return sprintf(buf, "1: enabled\n");
    }

    return sprintf(buf, "0: disabled\n");
}

static ssize_t store_enable(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned mode;
    ssize_t r;

    r = sscanf(buf, "%d", &mode);
    if ((r != 1)) {
        return -EINVAL;
    }

    if (!tsync_automute_on) {
        tsync_enable = mode ? 1 : 0;
    }

    return size;
}

static ssize_t show_discontinue(struct class *class,
                                struct class_attribute *attr,
                                char *buf)
{
	pts_discontinue = vpts_discontinue || apts_discontinue;
    if (pts_discontinue) {
        return sprintf(buf, "1: pts_discontinue, apts_discontinue_diff=%d, vpts_discontinue_diff=%d,\n",
			apts_discontinue_diff, vpts_discontinue_diff);
    }

    return sprintf(buf, "0: pts_continue\n");
}

static ssize_t store_discontinue(struct class *class,
                                 struct class_attribute *attr,
                                 const char *buf,
                                 size_t size)
{
    unsigned discontinue;
    ssize_t r;

    r = sscanf(buf, "%d", &discontinue);
    if ((r != 1)) {
        return -EINVAL;
    }

    pts_discontinue = discontinue ? 1 : 0;

    return size;
}

static ssize_t show_debug_pts_checkin(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
    if (debug_pts_checkin) {
        return sprintf(buf, "1: debug pts checkin on\n");
    }

    return sprintf(buf, "0: debug pts checkin off\n");
}

static ssize_t store_debug_pts_checkin(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned mode;
    ssize_t r;

    r = sscanf(buf, "%d", &mode);
    if ((r != 1)) {
        return -EINVAL;
    }

    debug_pts_checkin = mode ? 1 : 0;

    return size;
}

static ssize_t show_debug_pts_checkout(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
    if (debug_pts_checkout) {
        return sprintf(buf, "1: debug pts checkout on\n");
    }

    return sprintf(buf, "0: debug pts checkout off\n");
}

static ssize_t store_debug_pts_checkout(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned mode;
    ssize_t r;

    r = sscanf(buf, "%d", &mode);
    if ((r != 1)) {
        return -EINVAL;
    }

    debug_pts_checkout = mode ? 1 : 0;

    return size;
}

static ssize_t show_debug_vpts(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
    if (debug_vpts) {
        return sprintf(buf, "1: debug vpts on\n");
    }

    return sprintf(buf, "0: debug vpts off\n");
}

static ssize_t store_debug_vpts(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned mode;
    ssize_t r;

    r = sscanf(buf, "%d", &mode);
    if ((r != 1)) {
        return -EINVAL;
    }

    debug_vpts = mode ? 1 : 0;

    return size;
}

static ssize_t show_debug_apts(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
    if (debug_apts) {
        return sprintf(buf, "1: debug apts on\n");
    }

    return sprintf(buf, "0: debug apts off\n");
}

static ssize_t store_debug_apts(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned mode;
    ssize_t r;

    r = sscanf(buf, "%d", &mode);
    if ((r != 1)) {
        return -EINVAL;
    }

    debug_apts = mode ? 1 : 0;

    return size;
}

static ssize_t show_av_threshold_min(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{

  return sprintf(buf, "tsync_av_threshold_min=%d\n", tsync_av_threshold_min);
  
}

static ssize_t store_av_threshold_min(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned min;
    ssize_t r;
	
    r = sscanf(buf, "%d", &min);
    if (r != 1) {
        return -EINVAL;
    }
	
    tsync_set_av_threshold_min(min);
    return size;
}
static ssize_t show_av_threshold_max(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{

     return sprintf(buf, "tsync_av_threshold_max=%d\n", tsync_av_threshold_max);
	 
}

static ssize_t store_av_threshold_max(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned max;
    ssize_t r;

    r = sscanf(buf, "%d", &max);
    if (r != 1) {
        return -EINVAL;
    }

    tsync_set_av_threshold_max(max);
    return size;
}

static ssize_t show_last_checkin_apts(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
  unsigned int last_apts;
  last_apts = get_last_checkin_pts(PTS_TYPE_AUDIO);
  return sprintf(buf, "0x%x\n",last_apts);
}

static ssize_t show_firstvpts(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "0x%x\n", timestamp_firstvpts_get());
}

static ssize_t show_vpause_flag(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "0x%x\n", tsync_get_vpause_flag());
}

static ssize_t store_vpause_flag(struct class *class,
                         struct class_attribute *attr,
                         const char *buf,
                         size_t size)
{
    unsigned mode;
    ssize_t r;
    r = sscanf(buf, "%d", &mode);
    if (r != 1) {
        return -EINVAL;
    }
    tsync_set_vpause_flag(mode);
    return size;
}

static struct class_attribute tsync_class_attrs[] = {
    __ATTR(pts_video,  S_IRUGO | S_IWUSR | S_IWGRP, show_vpts,    store_vpts),
    __ATTR(pts_audio,  S_IRUGO | S_IWUSR | S_IWGRP, show_apts,    store_apts),
    __ATTR(dobly_av_sync,  S_IRUGO | S_IWUSR | S_IWGRP, dobly_show_sync,    dobly_store_sync),
    __ATTR(pts_pcrscr, 0666, show_pcrscr,  store_pcrscr),
    __ATTR(event,      S_IRUGO | S_IWUSR | S_IWGRP, NULL,         store_event),
    __ATTR(mode,       S_IRUGO | S_IWUSR | S_IWGRP, show_mode,    store_mode),
    __ATTR(enable,     0666, show_enable,  store_enable),
    __ATTR(pcr_recover, S_IRUGO | S_IWUSR | S_IWGRP, show_pcr_recover,  store_pcr_recover),
    __ATTR(discontinue, S_IRUGO | S_IWUSR, show_discontinue,  store_discontinue),
    __ATTR(debug_pts_checkin, S_IRUGO | S_IWUSR, show_debug_pts_checkin,  store_debug_pts_checkin),
    __ATTR(debug_pts_checkout, S_IRUGO | S_IWUSR, show_debug_pts_checkout,  store_debug_pts_checkout),
    __ATTR(debug_video_pts, S_IRUGO | S_IWUSR, show_debug_vpts,  store_debug_vpts),
    __ATTR(debug_audio_pts, S_IRUGO | S_IWUSR, show_debug_apts,  store_debug_apts),
    __ATTR(av_threshold_min, S_IRUGO | S_IWUSR | S_IWGRP, show_av_threshold_min,  store_av_threshold_min),
    __ATTR(av_threshold_max, S_IRUGO | S_IWUSR | S_IWGRP, show_av_threshold_max,  store_av_threshold_max),
    __ATTR(last_checkin_apts, S_IRUGO | S_IWUSR, show_last_checkin_apts, NULL),
    __ATTR(firstvpts, S_IRUGO | S_IWUSR, show_firstvpts, NULL),
    __ATTR(vpause_flag, S_IRUGO | S_IWUSR, show_vpause_flag, store_vpause_flag),
    __ATTR_NULL
};

static struct class tsync_class = {
        .name = "tsync",
        .class_attrs = tsync_class_attrs,
    };

static int __init tsync_init(void)
{
    int r;

    r = class_register(&tsync_class);

    if (r) {
        amlog_level(LOG_LEVEL_ERROR, "tsync class create fail.\n");
        return r;
    }

    /* init audio pts to -1, others to 0 */
    timestamp_apts_set(-1);
    timestamp_vpts_set(0);
    timestamp_pcrscr_set(0);

    init_timer(&tsync_pcr_recover_timer);

    tsync_pcr_recover_timer.function = tsync_pcr_recover_timer_func;
    tsync_pcr_recover_timer.expires = jiffies + PCR_CHECK_INTERVAL;
    pcr_sync_stat = PCR_SYNC_UNSET;
    pcr_recover_trigger = 0;

    add_timer(&tsync_pcr_recover_timer);
    
    
    init_timer(&tsync_state_switch_timer);
    tsync_state_switch_timer.function = tsync_state_switch_timer_fun;
    tsync_state_switch_timer.expires = jiffies + 1;

    add_timer(&tsync_state_switch_timer);
    return (0);
}

static void __exit tsync_exit(void)
{
    del_timer_sync(&tsync_pcr_recover_timer);

    class_unregister(&tsync_class);
}

module_init(tsync_init);
module_exit(tsync_exit);

MODULE_DESCRIPTION("AMLOGIC time sync management driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <timyao@amlogic.com>");
