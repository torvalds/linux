/*
 * Amlogic M2
 * frame buffer driver-----------Deinterlace
 * Copyright (C) 2010 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */


#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <mach/am_regs.h>

#include <linux/amlogic/osd/osd_dev.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/amports/canvas.h>
#include "deinterlace.h"
#include "deinterlace_module.h"
/*for hisense patch*/
int pd_enable = 0;
static uint field_diff_thresh = 0x12;
static uint frame_diff_thresh = 0x20;
static uint pd_pd1field_num   = 0x8;

module_param(pd_enable,int,0664);
MODULE_PARM_DESC(pd_enable,"\n pulldown detect for hisense enable\n");
module_param(field_diff_thresh,uint,0664);
MODULE_PARM_DESC(field_diff_thresh,"\n field different threshold\n");
module_param(frame_diff_thresh,uint,0664);
MODULE_PARM_DESC(frame_diff_thresh,"\n frame different threshold\n");
module_param(pd_pd1field_num,uint,0664);
MODULE_PARM_DESC(pd_pd1field_num,"\n the consecutive field consecutive check number\n");

/* enable/disble print info about pull down mode detection  */
static int print_en = 0;
module_param(print_en,int,0664);
MODULE_PARM_DESC(print_en,"\n enable/disable print\n");

#define PATTERN32_NUM		2
#define PATTERN22_NUM		32
#if (PATTERN22_NUM < 32)
#define PATTERN22_MARK		((1LL<<PATTERN22_NUM)-1)
#elif (PATTERN22_NUM < 64)
#define PATTERN22_MARK		((0x100000000LL<<(PATTERN22_NUM-32))-1)
#else
#define PATTERN22_MARK		0xffffffffffffffffLL
#endif

static int pattern_len[MAX_WIN_NUM+1] = {0, 0, 0, 0, 0, 0};
static int di_p32_counter[MAX_WIN_NUM+1] = {0, 0, 0, 0, 0, 0};
static unsigned int last_big_frame_diff[MAX_WIN_NUM+1] = {0, 0, 0, 0, 0, 0};
static unsigned int last_big_frame_diff_num[MAX_WIN_NUM+1] = {0, 0, 0, 0, 0, 0};
static unsigned long long di_p32_info[MAX_WIN_NUM+1], di_p22_info[MAX_WIN_NUM+1], di_p32_info_2[MAX_WIN_NUM+1], di_p22_info_2[MAX_WIN_NUM+1];

void reset_pulldown_state(void)
{
    int ii;
    for(ii=0; ii<(MAX_WIN_NUM+1); ii++){
        pattern_len[ii] = 0;
        di_p32_counter[ii] = 0;
        last_big_frame_diff[ii] = 0;
        last_big_frame_diff_num[ii] = 0;
        di_p32_info[ii] = 0;
        di_p32_info_2[ii] = 0;
        di_p22_info[ii] = 0;
        di_p22_info_2[ii] = 0;
    }
    reset_pd_his();
    reset_pd32_status();
}

void cal_pd_parameters(pulldown_detect_info_t* cur_info, pulldown_detect_info_t* pre_info, pulldown_detect_info_t* next_info,
                    pd_detect_threshold_t* pd_th)
{
    cur_info->frame_diff_skew =
        cur_info->frame_diff>pre_info->frame_diff ? cur_info->frame_diff-pre_info->frame_diff :
            pre_info->frame_diff-cur_info->frame_diff;
    cur_info->frame_diff_num_skew =
        cur_info->frame_diff_num>pre_info->frame_diff_num ? cur_info->frame_diff_num-pre_info->frame_diff_num :
            pre_info->frame_diff_num-cur_info->frame_diff_num;

    cur_info->field_diff_by_pre = (cur_info->field_diff==0)?0xff:pre_info->field_diff/cur_info->field_diff;
    cur_info->field_diff_by_next = (cur_info->field_diff==0)?0xff:next_info->field_diff/cur_info->field_diff;
    cur_info->field_diff_num_by_pre = (cur_info->field_diff_num==0)?0xff:pre_info->field_diff_num/cur_info->field_diff_num;
    cur_info->field_diff_num_by_next = (cur_info->field_diff_num==0)?0xff:next_info->field_diff_num/cur_info->field_diff_num;
    cur_info->frame_diff_by_pre = (cur_info->frame_diff==0)?0xff:pre_info->frame_diff/cur_info->frame_diff;
    cur_info->frame_diff_num_by_pre = (cur_info->frame_diff_num==0)?0xff:pre_info->frame_diff_num/cur_info->frame_diff_num;
    cur_info->frame_diff_skew_ratio = (cur_info->frame_diff_skew==0)?0xff:cur_info->frame_diff/cur_info->frame_diff_skew;
    cur_info->frame_diff_num_skew_ratio = (cur_info->frame_diff_num_skew==0)?0xff:cur_info->frame_diff_num/cur_info->frame_diff_num_skew;

    if( cur_info->field_diff_by_pre         > 0xff) cur_info->field_diff_by_pre         = 0xff;
    if( cur_info->field_diff_by_next        > 0xff) cur_info->field_diff_by_next        = 0xff;
    if( cur_info->field_diff_num_by_pre     > 0xff) cur_info->field_diff_num_by_pre     = 0xff;
    if( cur_info->field_diff_num_by_next    > 0xff) cur_info->field_diff_num_by_next    = 0xff;
    if( cur_info->frame_diff_by_pre         > 0xff) cur_info->frame_diff_by_pre         = 0xff;
    if( cur_info->frame_diff_num_by_pre     > 0xff) cur_info->frame_diff_num_by_pre     = 0xff;
    if( cur_info->frame_diff_skew_ratio     > 0xff) cur_info->frame_diff_skew_ratio     = 0xff;
    if( cur_info->frame_diff_num_skew_ratio > 0xff) cur_info->frame_diff_num_skew_ratio = 0xff;

    cur_info->field_diff_pattern = pre_info->field_diff_pattern<<1;
    cur_info->field_diff_num_pattern = pre_info->field_diff_num_pattern<<1;
    cur_info->frame_diff_pattern = pre_info->frame_diff_pattern<<1;
    cur_info->frame_diff_num_pattern = pre_info->frame_diff_num_pattern<<1;
    if(( cur_info->field_diff_by_pre > pd_th->field_diff_chg_th) &&
        ( cur_info->field_diff_by_next > pd_th->field_diff_chg_th)
        ){
        cur_info->field_diff_pattern |= 1;
    }
    if(( cur_info->field_diff_num_by_pre > pd_th->field_diff_num_chg_th) &&
        ( cur_info->field_diff_num_by_next > pd_th->field_diff_num_chg_th)
        ){
        cur_info->field_diff_num_pattern |= 1;
    }
    if( cur_info->frame_diff_by_pre > pd_th->frame_diff_chg_th){
        cur_info->frame_diff_pattern |= 1;
    }
    if( cur_info->frame_diff_num_by_pre > pd_th->frame_diff_num_chg_th){
        cur_info->frame_diff_num_pattern |= 1;
    }

}

static int check_p32_p22(pulldown_detect_info_t* cur_info, pulldown_detect_info_t* pre_info, pulldown_detect_info_t* pre2_info,
                        pd_detect_threshold_t* pd_th,int idx)
{

	di_p22_info[idx] = di_p22_info[idx] << 1;
 	di_p22_info_2[idx] = di_p22_info_2[idx] << 1;
 	di_p32_info[idx] = di_p32_info[idx] << 1;
 	di_p32_info_2[idx] = di_p32_info_2[idx] << 1;

	if ( cur_info->field_diff*pd_th->field_diff_chg_th <= pre_info->field_diff &&
	        pre2_info->field_diff*pd_th->field_diff_chg_th <= pre_info->field_diff &&
	        cur_info->field_diff_num*pd_th->field_diff_num_chg_th <= pre_info->field_diff_num &&
	        pre2_info->field_diff_num*pd_th->field_diff_num_chg_th <= pre_info->field_diff_num ){
		di_p22_info[idx] |= 1;
  }

	if ( (di_p22_info[idx] & 0x1) && cur_info->frame_diff_skew*pd_th->frame_diff_skew_th <= cur_info->frame_diff
	    && cur_info->frame_diff_num_skew*pd_th->frame_diff_num_skew_th <= cur_info->frame_diff_num ){
		di_p22_info_2[idx] |= 1;
  }

	if ( di_p32_counter[idx] > 0 || di_p32_info[idx] == 0 )
	{
		if ( cur_info->frame_diff*pd_th->frame_diff_chg_th <= pre_info->frame_diff &&
		        cur_info->frame_diff_num*pd_th->frame_diff_num_chg_th <= pre_info->frame_diff_num )
		{
			di_p32_info[idx] |= 1;
			last_big_frame_diff[idx] = pre_info->frame_diff;
			last_big_frame_diff_num[idx] = pre_info->frame_diff_num;
			di_p32_counter[idx] = -1;
		}
		else
		{
			last_big_frame_diff[idx] = 0;
			last_big_frame_diff_num[idx] = 0;

			if ( (di_p32_counter[idx] & 0x1) && cur_info->frame_diff_skew*pd_th->frame_diff_skew_th <= cur_info->frame_diff
			    && cur_info->frame_diff_num_skew*pd_th->frame_diff_num_skew_th <= cur_info->frame_diff_num )
				di_p32_info_2[idx] |= 1;
		}
	}
	else
	{
		if ( cur_info->frame_diff*pd_th->frame_diff_chg_th <= last_big_frame_diff[idx] &&
		    cur_info->frame_diff_num*pd_th->frame_diff_num_chg_th <= last_big_frame_diff_num[idx] )
		{
			di_p32_info[idx] |= 1;
			di_p32_counter[idx] = -1;
		}
	}

	di_p32_counter[idx]++;


	return 0;
}


void pattern_check_pre_2(int idx, pulldown_detect_info_t* cur_info, pulldown_detect_info_t* pre_info, pulldown_detect_info_t* pre2_info,
                    int* pre_pulldown_mode, int* pre2_pulldown_mode, int* type,
                    pd_detect_threshold_t* pd_th)
{

		check_p32_p22(cur_info, pre_info, pre2_info, pd_th, idx);

    *pre_pulldown_mode = -1;

			if ( ((di_p22_info[idx] & PATTERN22_MARK) == (0x5555555555555555LL & PATTERN22_MARK))
				&& ((di_p22_info_2[idx] & PATTERN22_MARK) == (0x5555555555555555LL & PATTERN22_MARK)) )
			{
				*pre_pulldown_mode = 1;
                *type = 0;

			}
			else if ( ((di_p22_info[idx] & PATTERN22_MARK) == (0xaaaaaaaaaaaaaaaaLL & PATTERN22_MARK))
				&& ((di_p22_info_2[idx] & PATTERN22_MARK) == (0xaaaaaaaaaaaaaaaaLL & PATTERN22_MARK)) )
			{
		    	*pre_pulldown_mode = 0;
                *type=0;
			}
			else if ( pattern_len[idx] == 0 ){
				if(pre2_pulldown_mode)
				    *pre2_pulldown_mode = -1;
		  }

			if ( pattern_len[idx] == 0 )
			{
				int i, j, pattern, pattern_2, mask;

				for ( j = 5 ; j < 22 ; j++ )
				{
					mask = (1<<j) - 1;
					pattern = di_p32_info[idx] & mask;
					pattern_2 = di_p32_info_2[idx] & mask;

					if ( pattern != 0 && pattern_2 != 0 && pattern != mask )
					{
						for ( i = j ; i < j*PATTERN32_NUM ; i += j )
							if ( ((di_p32_info[idx]>>i) & mask) != pattern || ((di_p32_info_2[idx]>>i) & mask) != pattern_2 )
								break;

						if ( i == j*PATTERN32_NUM )
						{
							if ( (pattern_len[idx] == 5) && ((pattern & (pattern-1)) == 0) )
							{
								if ( (di_p32_info[idx] & 0x1) || (di_p32_info[idx] & 0x2) || (di_p32_info[idx] & 0x8) )
									*pre_pulldown_mode = 0;
								else
									*pre_pulldown_mode = 1;
                                    *type = 1;
							}
							else
							{
								if ( (pattern & (pattern-1)) != 0 )
								{
									if ( cur_info->field_diff_num < pre_info->field_diff_num )
								    	*pre_pulldown_mode = 1;
									else
										*pre_pulldown_mode = 0;
                  *type=1;
								}
							}

							pattern_len[idx] = j;
							break;
						}
					}
				}
			}
			else
			{
				int i, pattern, pattern_2, mask;

				mask = (1<<pattern_len[idx]) - 1;
				pattern = di_p32_info[idx] & mask;
				pattern_2 = di_p32_info_2[idx] & mask;

				for ( i = pattern_len[idx] ; i < pattern_len[idx]*PATTERN32_NUM ; i += pattern_len[idx] )
					if ( ((di_p32_info[idx]>>i) & mask) != pattern || ((di_p32_info_2[idx]>>i) & mask) != pattern_2 )
						break;

				if ( i == pattern_len[idx]*PATTERN32_NUM ) //// if is pal and tuner input ,set unenable
				{
					if ( (pattern_len[idx] == 5) && ((pattern & (pattern-1)) == 0) )
					{
						if ( (di_p32_info[idx] & 0x1) || (di_p32_info[idx] & 0x2) || (di_p32_info[idx] & 0x8) )
							*pre_pulldown_mode = 0;
						else
							*pre_pulldown_mode = 1;
                            *type=1;
					}
					else
					{
						if ( (pattern & (pattern-1)) != 0 )
						{
							if ( cur_info->field_diff_num < pre_info->field_diff_num )
						    	*pre_pulldown_mode = 1;
							else
								*pre_pulldown_mode = 0;
                                *type=1;
						}
					}
				}
				else
				{
					pattern_len[idx] = 0;
					if(pre2_pulldown_mode)
					    *pre2_pulldown_mode = -1;
				}
			}
}


/* new PD algorithm */
#define PD_HIS_NUM         1024
typedef struct{
    unsigned frame_diff;
    unsigned frame_diff_num;
    unsigned field_diff;
    unsigned field_diff_num;
}pd_his_t;

static pd_his_t pd_his_pool[PD_HIS_NUM*2];
static unsigned pd_his_wr_pos = 0;
static unsigned pd_his_size = 0;

#define pd_his(i, pattern_len)  (&pd_his_pool[pd_his_wr_pos+PD_HIS_NUM-pattern_len+i])

void reset_pd_his(void)
{
    pd_his_wr_pos = 0;
    pd_his_size = 0;
}

void insert_pd_his(pulldown_detect_info_t* pd_info)
{
    pd_his_t* phis = &pd_his_pool[pd_his_wr_pos];
    phis->frame_diff = (phis+PD_HIS_NUM)->frame_diff = pd_info->frame_diff;
    phis->frame_diff_num = (phis+PD_HIS_NUM)->frame_diff_num = pd_info->frame_diff_num;
    phis->field_diff = (phis+PD_HIS_NUM)->field_diff = pd_info->field_diff;
    phis->field_diff_num = (phis+PD_HIS_NUM)->field_diff_num = pd_info->field_diff_num;
    pd_his_wr_pos++;
    if(pd_his_wr_pos >= PD_HIS_NUM){
        pd_his_wr_pos = 0;
    }
    if(pd_his_size < PD_HIS_NUM){
        pd_his_size++;
    }
}

/* algorithm to detect pd32 */
unsigned int pd32_match_num=0x10;
unsigned int pd32_diff_num_0_th=1;
unsigned int pd32_match_num_th;
unsigned int pd32_debug_th =0;
unsigned int pd22_th = 0x3;
unsigned int pd22_num_th = 0x5;
unsigned int pd22_match_num = 0x5;
/*  ...
       A-odd
       A-even
       A-odd              cur_pd32_status = 1
       B-even             cur_pd32_status = 2
       B-odd              cur_pd32_status = 3
       C-even             cur_pd32_status = 4
       C-odd              cur_pd32_status = 5
       C-even             cur_pd32_status = 1
       D-odd
       D-even
    ...
*/
static int cur_pd22_status = 0;

static int cur_pd32_status = 0;
static unsigned int last_small_frame_diff_num =0;
static unsigned int pattern_match_count = 0;
static unsigned int pd32_diff_num_0_count = 0;
//static unsigned int pd22_num = 0 ;
void reset_pd32_status(void)
{
    cur_pd22_status = 0;
    cur_pd32_status = 0;
    last_small_frame_diff_num = 0;
    pattern_match_count = 0;
    pd32_diff_num_0_count = 0;
    /**/
    pd32_match_num_th = pd32_match_num;
}

int detect_pd32(void)
{
    int blend_mode = -1;
    int i, ii;
    int  pd_pd1field = 0;
    pd_his_t* phis;
    //pd_his_t* phis_22;
    //unsigned pd22_pattern_len = pd22_match_num*4;
    unsigned pd32_pattern_len = pd32_match_num*5;
    static int cur_pd1field_status;
/*
	   phis_22 = pd_his(pd22_pattern_len-1,pd22_pattern_len);
	   if (pd22_th==0xff)
	   {
 		printk("phis_22->field_diff_num = %08x\n",phis_22->field_diff_num );
	   }
	   if (phis_22->field_diff_num >((phis_22-1)->field_diff_num*pd22_th)){
	   	blend_mode  =  1;
	   	for (ii = 0; ii < (pd22_pattern_len>>1) ; ii ++){
             		if (((phis_22-2*ii)->field_diff_num <= ((phis_22-2*ii-1)->field_diff_num * pd22_th)) ||
		    	(((phis_22-2*ii-1)->field_diff_num *pd22_th) > (phis_22-2*(ii+1))->field_diff_num))	{
			blend_mode = -1;
			break;
			}
	        }
	    }
	    else if ((phis_22->field_diff_num*pd22_th) <=(phis_22-1)->field_diff_num)
	    {
		 blend_mode = 0;
		 for (ii = 0; ii < (pd22_pattern_len>>1) ; ii ++){
             		if ((((phis_22-2*ii)->field_diff_num * pd22_th ) > (phis_22-2*ii-1)->field_diff_num ) ||
		    	(((phis_22-2*ii-1)->field_diff_num) <=(( phis_22-2*ii-2)->field_diff_num * pd22_th)))	{
			blend_mode = -1;
			break;
			}
	        }
	     }
            else
            {
	   	blend_mode = -1;
*/
	if(cur_pd32_status){
        phis = pd_his(pd32_pattern_len-1,pd32_pattern_len);
        cur_pd32_status++;
        if(cur_pd32_status>5){
            cur_pd32_status=1;
        }
        if(cur_pd32_status>1){
            if(pattern_match_count==pd32_match_num_th){
                if(phis->frame_diff_num<last_small_frame_diff_num){
                    pattern_match_count--;
                    if(pattern_match_count<pd32_match_num_th){
                        reset_pd32_status();
                    }
                }
            }
        }
        else{
            unsigned tmp_count;
            if(pattern_match_count>pd32_match_num_th){
                for(ii=1;ii<5;ii++){
                    if((phis-ii)->frame_diff_num <= (phis-5)->frame_diff_num)
                        break;
                }
                if(ii<5){
                    pattern_match_count--;
                }
            }

            tmp_count=phis->frame_diff_num;
            for(ii=1;ii<5;ii++){
                if((phis-ii)->frame_diff_num <= phis->frame_diff_num)
                    break;
                tmp_count+=(phis-ii)->frame_diff_num;
            }
            if((tmp_count==0)&&(ii==5)){
                pd32_diff_num_0_count++;
                if(pd32_diff_num_0_count>pd32_match_num){
                    pd32_diff_num_0_count=pd32_match_num;
                }
            }
            else{
                if(pd32_diff_num_0_count>0){
                    pd32_diff_num_0_count--;
                }
            }
            if(ii<5){
                pattern_match_count--;
            }
            else{
                pattern_match_count++;
                if(pattern_match_count>pd32_match_num){
                    pattern_match_count=pd32_match_num;
                }
            }

            if((pattern_match_count<pd32_match_num_th)
                ||(pd32_diff_num_0_count>pd32_diff_num_0_th)){
                reset_pd32_status();
            }
            if(cur_pd32_status>0){
                last_small_frame_diff_num = phis->frame_diff_num;
            }
        }
    }
    if((cur_pd32_status==0)&&(pd_his_size>=pd32_pattern_len)){
        phis = pd_his(0,pd32_pattern_len);
        pd32_diff_num_0_count = 0;
        pattern_match_count=0;
        for( i=0; i<pd32_pattern_len; i+=5){
            unsigned tmp_count=(phis+4)->frame_diff_num;
            for(ii=0;ii<4;ii++){
                if((phis+ii)->frame_diff_num < ((phis+4)->frame_diff_num)<<1)
                    break;
                tmp_count+=(phis+ii)->frame_diff_num;
            }
            if((tmp_count==0)&&(ii==4)){
                pd32_diff_num_0_count++;
            }
            if(ii==4){
                if((i+5)<pd32_pattern_len){
                    for(ii=0;ii<4;ii++){
                        if((phis+5+ii)->frame_diff_num < ((phis+4)->frame_diff_num)<<1)
                            break;
                    }
                    if(ii==4){
                        pattern_match_count++;
                    }
                }
                else{
                    pattern_match_count++;
                }
            }
            phis+=5;
        }
        if((pattern_match_count>=pd32_match_num_th)
            &&(pd32_diff_num_0_count<=pd32_diff_num_0_th)){
            cur_pd32_status = 1;
            last_small_frame_diff_num = pd_his(pd32_pattern_len-1,pd32_pattern_len)->frame_diff_num;
        }
    }
    if(cur_pd32_status > 0){
        if(cur_pd32_status==2 || cur_pd32_status==4){
            blend_mode=1; //blend with next
        }
        else{
            blend_mode=0; //blend with pervious
        }
    }
    /*for hisense static 1 filed output*/
    if ((blend_mode == -1) && pd_enable){
        phis = pd_his(pd_pd1field_num-1,pd_pd1field_num);
        cur_pd1field_status = 1;
        blend_mode = 2;
        if (cur_pd1field_status){
	    pd_pd1field = 1;
	    for (ii = 0; ii < pd_pd1field_num ;ii ++){
	        if (((phis+ii)->field_diff_num > field_diff_thresh) || ((phis+ii)->frame_diff_num > frame_diff_thresh)){
		        cur_pd1field_status = 0;
	            blend_mode = -1;
		     pd_pd1field = 0;
		     break;
	        }
	    }
        }
        else{
	    pd_pd1field = 1;
	    for (ii = 0; ii < pd_pd1field_num -1;ii ++){
  	        if (((phis+ii)->field_diff_num > field_diff_thresh) && ((phis+ii)->frame_diff_num > frame_diff_thresh)){
		  pd_pd1field = 0;
		  break;
	        }
	    }
	    if (pd_pd1field) {
            cur_pd1field_status =1;
	        blend_mode = 2;
	    }
        }
        if((blend_mode == 2)&& pd_enable && print_en)
        {
            pr_info("%s:cur_fd1field_status is %d,blend_mode is \
                %d,phis->field_diff_num:%u,phis->frame_diff_num:%u\n",__func__,\
                cur_pd1field_status,blend_mode,(phis+ii)->field_diff_num,(phis+ii)->frame_diff_num);
        }
    }

    return blend_mode;
}

