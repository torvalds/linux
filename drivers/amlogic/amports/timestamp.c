#include <linux/module.h>
#include <linux/amlogic/amports/tsync.h>
#include <mach/am_regs.h>
unsigned int timestamp_enable_resample_flag = 0;
EXPORT_SYMBOL(timestamp_enable_resample_flag);
unsigned int timestamp_resample_type_flag = 0;
EXPORT_SYMBOL(timestamp_resample_type_flag);
u32 acc_apts_inc = 0;
u32 acc_apts_dec = 0;
u32 acc_pcrscr_inc = 0;
u32 acc_pcrscr_dec = 0;
/*need match to libplayer resample lib*/
#define DEFALT_NUMSAMPS_PERCH   128
extern int resample_delta;

static s32 system_time_inc_adj = 0;
static u32 system_time = 0;
static u32 system_time_up = 0;
static u32 audio_pts_up = 0;
static u32 audio_pts_started = 0;
static u32 first_vpts = 0;
static u32 first_apts = 0;

static u32 system_time_scale_base = 1;
static u32 system_time_scale_remainder = 0;

#ifdef MODIFY_TIMESTAMP_INC_WITH_PLL
#define PLL_FACTOR 10000
static u32 timestamp_inc_factor=PLL_FACTOR;
void set_timestamp_inc_factor(u32 factor)
{
	timestamp_inc_factor = factor;
}
#endif


u32 timestamp_vpts_get(void)
{
    return READ_MPEG_REG(VIDEO_PTS);
}

EXPORT_SYMBOL(timestamp_vpts_get);

void timestamp_vpts_set(u32 pts)
{
    WRITE_MPEG_REG(VIDEO_PTS, pts);
}

EXPORT_SYMBOL(timestamp_vpts_set);

void timestamp_vpts_inc(s32 val)
{
    WRITE_MPEG_REG(VIDEO_PTS, READ_MPEG_REG(VIDEO_PTS) + val);
}

EXPORT_SYMBOL(timestamp_vpts_inc);

u32 timestamp_apts_get(void)
{
    return READ_MPEG_REG(AUDIO_PTS);
}

EXPORT_SYMBOL(timestamp_apts_get);

void timestamp_apts_set(u32 pts)
{
    WRITE_MPEG_REG(AUDIO_PTS, pts);
}

EXPORT_SYMBOL(timestamp_apts_set);

void timestamp_apts_inc(s32 inc)
{
	if(audio_pts_up){
#ifdef MODIFY_TIMESTAMP_INC_WITH_PLL
	inc = inc*timestamp_inc_factor/PLL_FACTOR;
#endif
    if(tsync_get_mode()!=TSYNC_MODE_PCRMASTER){//timestamp_enable_resample_flag){
		if(timestamp_resample_type_flag==0){      
			//0-->no resample  processing
		}else if(timestamp_resample_type_flag==1){//1-->down resample processing
				inc += inc*resample_delta / DEFALT_NUMSAMPS_PERCH;
				acc_apts_inc += inc*resample_delta % DEFALT_NUMSAMPS_PERCH;
				if(acc_apts_inc*resample_delta >= DEFALT_NUMSAMPS_PERCH){
					inc += acc_apts_inc*resample_delta / DEFALT_NUMSAMPS_PERCH;
					acc_apts_inc = acc_apts_inc*resample_delta % DEFALT_NUMSAMPS_PERCH;
				}			
		}else if(timestamp_resample_type_flag==2){//2-->up resample processing
				inc -= inc*resample_delta / DEFALT_NUMSAMPS_PERCH;
				acc_apts_dec += inc*resample_delta % DEFALT_NUMSAMPS_PERCH;
				if(acc_apts_dec*resample_delta >= DEFALT_NUMSAMPS_PERCH){
					inc -= acc_apts_dec*resample_delta / DEFALT_NUMSAMPS_PERCH;
					acc_apts_dec = acc_apts_dec*resample_delta % DEFALT_NUMSAMPS_PERCH;
				}			
		}
	}
    WRITE_MPEG_REG(AUDIO_PTS, READ_MPEG_REG(AUDIO_PTS) + inc);
	}
}

EXPORT_SYMBOL(timestamp_apts_inc);

void timestamp_apts_enable(u32 enable)
{
    audio_pts_up = enable;
    printk("timestamp_apts_enable enable:%x, \n", enable);
}

EXPORT_SYMBOL(timestamp_apts_enable);

void timestamp_apts_start(u32 enable)
{
  audio_pts_started = enable;
  printk("audio pts started::::::: %d\n", enable);
}
EXPORT_SYMBOL(timestamp_apts_start);

u32 timestamp_apts_started(void)
{
  return audio_pts_started;
}
EXPORT_SYMBOL(timestamp_apts_started);


u32 timestamp_pcrscr_get(void)
{
    return system_time;
}

EXPORT_SYMBOL(timestamp_pcrscr_get);

void timestamp_pcrscr_set(u32 pts)
{
    system_time = pts;
}

EXPORT_SYMBOL(timestamp_pcrscr_set);

void timestamp_firstvpts_set(u32 pts)
{
    first_vpts = pts;
    printk("video first pts = %x\n", first_vpts);
}

EXPORT_SYMBOL(timestamp_firstvpts_set);

u32 timestamp_firstvpts_get(void)
{
    return first_vpts;
}
EXPORT_SYMBOL(timestamp_firstvpts_get);

void timestamp_firstapts_set(u32 pts)
{
    first_apts = pts;
    printk("audio first pts = %x\n", first_apts);
}

EXPORT_SYMBOL(timestamp_firstapts_set);

u32 timestamp_firstapts_get(void)
{
    return first_apts;
}
EXPORT_SYMBOL(timestamp_firstapts_get);

void timestamp_pcrscr_inc(s32 inc)
{
    if (system_time_up) {
#ifdef MODIFY_TIMESTAMP_INC_WITH_PLL
        inc = inc*timestamp_inc_factor/PLL_FACTOR;
#endif
		if(tsync_get_mode()!=TSYNC_MODE_PCRMASTER){//timestamp_enable_resample_flag){
			if(timestamp_resample_type_flag==0){	  //0-->no resample  processing
				
			}else if(timestamp_resample_type_flag==1){//1-->down resample processing
				inc += inc*resample_delta / DEFALT_NUMSAMPS_PERCH;
		 		acc_pcrscr_inc += inc*resample_delta % DEFALT_NUMSAMPS_PERCH;
				if(acc_pcrscr_inc*resample_delta >= DEFALT_NUMSAMPS_PERCH){
					inc += acc_pcrscr_inc*resample_delta / DEFALT_NUMSAMPS_PERCH;
					acc_pcrscr_inc = acc_pcrscr_inc*resample_delta % DEFALT_NUMSAMPS_PERCH;
				}
			}else if(timestamp_resample_type_flag==2){//2-->up resample processing
				inc -= inc*resample_delta / DEFALT_NUMSAMPS_PERCH;
		 		acc_pcrscr_dec += inc*resample_delta % DEFALT_NUMSAMPS_PERCH;
				if(acc_pcrscr_dec*resample_delta >= DEFALT_NUMSAMPS_PERCH){
					inc -= acc_pcrscr_dec*resample_delta / DEFALT_NUMSAMPS_PERCH;
					acc_pcrscr_dec = acc_pcrscr_dec*resample_delta % DEFALT_NUMSAMPS_PERCH;
				}
			}
		}  
        system_time += inc + system_time_inc_adj;
    }
}

EXPORT_SYMBOL(timestamp_pcrscr_inc);

void timestamp_pcrscr_inc_scale(s32 inc, u32 base)
{
    if (system_time_scale_base != base) {
        system_time_scale_remainder = system_time_scale_remainder * base / system_time_scale_base;
        system_time_scale_base = base;
    }

    if (system_time_up) {
        u32 r;
        system_time += div_u64_rem(90000ULL * inc, base, &r) + system_time_inc_adj;
        system_time_scale_remainder += r;
        if (system_time_scale_remainder >= system_time_scale_base) {
            system_time++;
            system_time_scale_remainder -= system_time_scale_base;
        }
    }
}

EXPORT_SYMBOL(timestamp_pcrscr_inc_scale);

void timestamp_pcrscr_set_adj(s32 inc)
{
    system_time_inc_adj = inc;
}

EXPORT_SYMBOL(timestamp_pcrscr_set_adj);

void timestamp_pcrscr_enable(u32 enable)
{
    system_time_up = enable;
}

EXPORT_SYMBOL(timestamp_pcrscr_enable);

u32 timestamp_pcrscr_enable_state(void)
{
    return system_time_up;
}

EXPORT_SYMBOL(timestamp_pcrscr_enable_state);

MODULE_DESCRIPTION("AMLOGIC time sync management driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <timyao@amlogic.com>");
