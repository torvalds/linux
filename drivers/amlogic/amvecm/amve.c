/*
 * Video Enhancement
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *         Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/module.h>


#include <mach/am_regs.h>
#include <linux/amlogic/amstream.h>
#include <linux/amlogic/ve.h>

#include <linux/amlogic/aml_common.h>
#include <linux/amlogic/vframe.h>

#include "ve_regs.h"
#include "amve.h"

// 0: Invalid
// 1: Valid
// 2: Updated in 2D mode
// 3: Updated in 3D mode
unsigned long flags;
//#define NEW_DNLP_AFTER_PEAKING

struct ve_hist_s video_ve_hist;

static unsigned char ve_dnlp_tgt[64];
bool ve_en;
unsigned int ve_dnlp_white_factor;
unsigned int ve_dnlp_rt = 0;
unsigned int ve_dnlp_rl;
unsigned int ve_dnlp_black;
unsigned int ve_dnlp_white;
unsigned int ve_dnlp_luma_sum;
static ulong ve_dnlp_lpf[64], ve_dnlp_reg[16];

static bool frame_lock_nosm = 1;
static int ve_dnlp_waist_h = 128;
static int ve_dnlp_waist_l = 128;
static int ve_dnlp_ankle = 16;
static int ve_dnlp_strength = 255;

module_param(ve_dnlp_waist_h, int, 0664);
MODULE_PARM_DESC(ve_dnlp_waist_h, "ve_dnlp_waist_h");
module_param(ve_dnlp_waist_l, int, 0664);
MODULE_PARM_DESC(ve_dnlp_waist_l, "ve_dnlp_waist_l");
module_param(ve_dnlp_ankle, int, 0664);
MODULE_PARM_DESC(ve_dnlp_ankle, "ve_dnlp_ankle");
module_param(ve_dnlp_strength, int, 0664);
MODULE_PARM_DESC(ve_dnlp_strength, "ve_dnlp_strength");

static int dnlp_respond = 0;
module_param(dnlp_respond, int, 0664);
MODULE_PARM_DESC(dnlp_respond, "dnlp_respond");

static int dnlp_debug = 0;
module_param(dnlp_debug, int, 0664);
MODULE_PARM_DESC(dnlp_debug, "dnlp_debug");

static int ve_dnlp_method = 0;
module_param(ve_dnlp_method, int, 0664);
MODULE_PARM_DESC(ve_dnlp_method, "ve_dnlp_method");

static int ve_dnlp_cliprate = 6;
module_param(ve_dnlp_cliprate, int, 0664);
MODULE_PARM_DESC(ve_dnlp_cliprate, "ve_dnlp_cliprate");

static int ve_dnlp_lowrange = 18;
module_param(ve_dnlp_lowrange, int, 0664);
MODULE_PARM_DESC(ve_dnlp_lowrange, "ve_dnlp_lowrange");

static int ve_dnlp_hghrange = 18;
module_param(ve_dnlp_hghrange, int, 0664);
MODULE_PARM_DESC(ve_dnlp_hghrange, "ve_dnlp_hghrange");

static int ve_dnlp_lowalpha = 24;
module_param(ve_dnlp_lowalpha, int, 0664);
MODULE_PARM_DESC(ve_dnlp_lowalpha, "ve_dnlp_lowalpha");

static int ve_dnlp_midalpha = 24;
module_param(ve_dnlp_midalpha, int, 0664);
MODULE_PARM_DESC(ve_dnlp_midalpha, "ve_dnlp_midalpha");

static int ve_dnlp_hghalpha = 24;
module_param(ve_dnlp_hghalpha, int, 0664);
MODULE_PARM_DESC(ve_dnlp_hghalpha, "ve_dnlp_hghalpha");

module_param(frame_lock_nosm, bool, 0664);
MODULE_PARM_DESC(frame_lock_nosm, "frame_lock_nosm");

static int dnlp_adj_level = 6;
module_param(dnlp_adj_level, int, 0664);
MODULE_PARM_DESC(dnlp_adj_level, "dnlp_adj_level");

// ***************************************************************************
// *** VPP_FIQ-oriented functions *********************************************
// ***************************************************************************
static void ve_hist_gamma_tgt(vframe_t *vf)
{
    struct vframe_prop_s *p = &vf->prop;
    video_ve_hist.sum    = p->hist.luma_sum;
    video_ve_hist.width  = p->hist.width;
    video_ve_hist.height = p->hist.height;
}

static void ve_dnlp_calculate_tgt_ext(vframe_t *vf)
{
    struct vframe_prop_s *p = &vf->prop;
	static unsigned int sum_b = 0, sum_c = 0;
    ulong i = 0, j = 0, sum = 0, ave = 0, ankle = 0, waist = 0, peak = 0, start = 0;
    ulong qty_h = 0, qty_l = 0, ratio_h = 0, ratio_l = 0;
    ulong div1  = 0, div2  = 0, step_h  = 0, step_l  = 0;
    ulong data[55];
    bool  flag[55], previous_state_high = false;
	unsigned int cnt = READ_CBUS_REG(ASSIST_SPARE8_REG1);

    // old historic luma sum
    sum_b = sum_c;
    sum_c = ve_dnlp_luma_sum;
    // new historic luma sum
    ve_dnlp_luma_sum = p->hist.luma_sum;
	if(dnlp_debug)
	printk("ve_dnlp_luma_sum=%x,sum_b=%x,sum_c=%x\n",ve_dnlp_luma_sum,sum_b,sum_c);

    // picture mode: freeze dnlp curve
    if(dnlp_respond)
    {
		if (// new luma sum is 0, something is wrong, freeze dnlp curve
			(!ve_dnlp_luma_sum)
		   )
		return;
    }
	else
	{
	    if (// new luma sum is 0, something is wrong, freeze dnlp curve
	        (!ve_dnlp_luma_sum) ||
	        // new luma sum is closed to old one (1 +/- 1/64), picture mode, freeze curve
	        ((ve_dnlp_luma_sum < sum_b + (sum_b >> dnlp_adj_level)) &&
	         (ve_dnlp_luma_sum > sum_b - (sum_b>> dnlp_adj_level))
	        )
	       )
	    return;
	}

    // calculate ave (55 times of ave & data[] for accuracy)
    // ave    22-bit
    // data[] 22-bit
    //printk("ankle=%d,waist_h=%d,waist_l=%d,strenth=%d\n",ve_dnlp_ankle,ve_dnlp_waist_h,ve_dnlp_waist_l,ve_dnlp_strength);
    ave = 0;
    for (i = 0; i < 55; i++)
    {
        data[i]  = (ulong)p->hist.gamma[i + 4];
        ave     += data[i];
        data[i] *= 55;
        flag[i]  = false;
    }

    // calculate ankle
    // ankle 22-bit
    // waist 22-bit
    // qty_h 6-bit
    ankle = (ave * ve_dnlp_ankle+ 128) >> 8;

    // scan data[] to find out waist pulses
    qty_h = 0;
    previous_state_high = false;
    for (i = 0; i < 55; i++)
    {
        if (data[i] >= ankle)
        {
            // ankle pulses start
            if (!previous_state_high)
            {
                previous_state_high = true;
                start = i;
                peak = 0;
            }
            // normal maintenance
            if (peak < data[i])
                peak = data[i];
        }
        else
        {
            // ankle pulses end + 1
            if (previous_state_high)
            {
                previous_state_high = false;
                // calculate waist of high area pulses
                if (peak >= ave)
                    waist = ((peak - ankle) * ve_dnlp_waist_h + 128) >> 8;
	                // calculate waist of high area pulses
	                else
	                    waist = ((peak - ankle) * ve_dnlp_waist_l + 128) >> 8;
	                // find out waist pulses
	                for (j = start; j < i; j++)
	                {
	                    if (data[j] >= waist)
	                    {
	                        flag[j] = true;
	                        qty_h++;
	                    }
	                }
	            }
        }
    }

    // calculate ratio_h and ratio_l (div2 = 512*H*L times of value for accuracy)
    // averaged duty > 1/3
    // qty_l 6-bit
    // div1 20-bit
    // div2 21-bit
    // ratio_h 22-bit
    // ratio_l 21-bit
    qty_l =  55 - qty_h;
	if ((!qty_h) || (!qty_l))
		{
			for (i = 5; i <= 58; i++)
			{
				ve_dnlp_tgt[i] = i << 2;
			}
		}
	else
		{
		    div1  = 256 * qty_h * qty_l;
		    div2  = 512 * qty_h * qty_l;
		    if (qty_h > 18)
		    {
		        ratio_h = div2 + ve_dnlp_strength * qty_l * qty_l; // [1.0 ~ 2.0)
		        ratio_l = div2 - ve_dnlp_strength * qty_h * qty_l; // [0.5 ~ 1.0]
		    }
		    // averaged duty < 1/3
		    {
		        ratio_h = div2 + (ve_dnlp_strength << 1) * qty_h * qty_l; // [1.0 ~ 2.0]
		        ratio_l = div2 - (ve_dnlp_strength << 1) * qty_h * qty_h; // (0.5 ~ 1.0]
		    }

		    // distribute ratio_h & ratio_l to ve_dnlp_tgt[5] ~ ve_dnlp_tgt[58]
		    // sum 29-bit
		    // step_h 24-bit
		    // step_l 23-bit

		    sum = div2 << 4; // start from 16
		    step_h = ratio_h << 2;
		    step_l = ratio_l << 2;
		    for (i = 5; i <= 58; i++)
		    {
		        // high phase
		        if (flag[i - 5])
		            sum += step_h;
		        // low  phase
		        else
		            sum += step_l;
		        ve_dnlp_tgt[i] = (sum + div1) / div2;
		    }
			if(cnt)
			{
			for(i=0;i<64;i++)
				printk(" ve_dnlp_tgte[%ld]=%d\n",i,ve_dnlp_tgt[i]);
			WRITE_CBUS_REG(ASSIST_SPARE8_REG1, 0);
			}
		}

    // calculate black extension

    // calculate white extension
}

void GetWgtLst(ulong *iHst, ulong tAvg, ulong nLen, ulong alpha)
{
	ulong iMax=0;
	ulong iMin=0;
	ulong iPxl=0;
	ulong iT=0;

	for(iT=0;iT<nLen;iT++)
	{
		iPxl = iHst[iT];
		if(iPxl>tAvg)
		{
			iMax=iPxl;
			iMin=tAvg;
		}
		else
		{
			iMax=tAvg;
			iMin=iPxl;
		}

		if(alpha<16)
		{
			iPxl = ((16-alpha)*iMin+8)>>4;
			iPxl += alpha*iMin;
		}
		else if(alpha<32)
		{
			iPxl = (32-alpha)*iMin;
			iPxl += (alpha-16)*iMax;
		}
		else
		{
			iPxl = (48-alpha)+4*(alpha-32);
			iPxl *= iMax;
		}

		iPxl = (iPxl+8)>>4;

		iHst[iT] = iPxl<1 ? 1 : iPxl;
	}
}

static void ve_dnlp_calculate_tgtx(vframe_t *vf)
{
    struct vframe_prop_s *p = &vf->prop;
    ulong iHst[64];
    ulong oHst[64];
	static unsigned int sum_b = 0, sum_c = 0;
    ulong i = 0, j = 0, sum = 0, max = 0;
	ulong cLmt=0, nStp=0, stp=0, uLmt=0;
	long nExc=0;
	unsigned int cnt = READ_CBUS_REG(ASSIST_SPARE8_REG1);
	unsigned int cnt2 = READ_CBUS_REG(ASSIST_SPARE8_REG2);

	unsigned int clip_rate = ve_dnlp_cliprate; //8bit
	unsigned int low_range = ve_dnlp_lowrange;//18; //6bit [0-54]
	unsigned int hgh_range = ve_dnlp_hghrange;//18; //6bit [0-54]
	unsigned int low_alpha = ve_dnlp_lowalpha;//24; //6bit [0--48]
	unsigned int mid_alpha = ve_dnlp_midalpha;//24; //6bit [0--48]
	unsigned int hgh_alpha = ve_dnlp_hghalpha;//24; //6bit [0--48]
	//-------------------------------------------------
	ulong tAvg=0;
	ulong nPnt=0;
	ulong mRng=0;


    // old historic luma sum
    sum_b = sum_c;
    sum_c = ve_dnlp_luma_sum;
    // new historic luma sum
    ve_dnlp_luma_sum = p->hist.luma_sum;
	if(dnlp_debug)
	printk("ve_dnlp_luma_sum=%x,sum_b=%x,sum_c=%x\n",ve_dnlp_luma_sum,sum_b,sum_c);

    // picture mode: freeze dnlp curve
    if(dnlp_respond)
    {
		if (// new luma sum is 0, something is wrong, freeze dnlp curve
			(!ve_dnlp_luma_sum)
		   )
		return;
    }
	else
	{
	    if (// new luma sum is 0, something is wrong, freeze dnlp curve
	        (!ve_dnlp_luma_sum) ||
	        // new luma sum is closed to old one (1 +/- 1/64), picture mode, freeze curve
	        ((ve_dnlp_luma_sum < sum_b + (sum_b >> dnlp_adj_level)) &&
	         (ve_dnlp_luma_sum > sum_b - (sum_b >> dnlp_adj_level))
	        )
	       )
	    return;
	}

    // 64 bins, max, ave
    for (i = 0; i < 64; i++)
    {
        iHst[i] = (ulong)p->hist.gamma[i];

		if(i>=4 && i<=58) { //55 bins
			oHst[i] = iHst[i];

			if (max < iHst[i])
				max = iHst[i];
			sum += iHst[i];
		}
		else {
			oHst[i] = 0;
		}
    }
	cLmt = (clip_rate*sum)>>8;
	tAvg = sum/55;


    // invalid histgram: freeze dnlp curve
    if (max<=55)
        return;

    // get 1st 4 points
    for (i = 4; i <= 58; i++)
    {
		if(iHst[i]>cLmt)
			nExc += (iHst[i]-cLmt);
    }
	nStp = (nExc+28)/55;
	uLmt = cLmt-nStp;
    if(cnt2)
    	{
	    printk(" ve_dnlp_tgtx:cLmt=%ld,nStp=%ld,uLmt=%ld\n",cLmt,nStp,uLmt);
		WRITE_CBUS_REG(ASSIST_SPARE8_REG2, 0);
    	}
    if(clip_rate<=4 || tAvg<=2)
    {
        cLmt = (sum+28)/55;
        sum = cLmt*55;

        for(i=4; i<=58; i++)
        {
             oHst[i] = cLmt;
        }
    }
    else if(nStp!=0)
    {
		for(i=4; i<=58; i++)
			{
				if(iHst[i]>=cLmt)
					oHst[i] = cLmt;
				else {
					if(iHst[i]>uLmt)
					{
						oHst[i] = cLmt;
						nExc -= cLmt-iHst[i];
					}
					else
					{
						oHst[i] = iHst[i]+nStp;
						nExc -= nStp;
					}
                    if(nExc<0 )
					nExc = 0;
				}
			}

        j=4;
        while(nExc>0) {
            if(nExc>=55)
            {
                nStp = 1;
                stp = nExc/55;
            }
            else
            {
                nStp = 55/nExc;
                stp = 1;
            }
            for(i=j;i<=58;i+=nStp)
			{
                if(oHst[i]<cLmt)
                {
                    oHst[i] += stp;
                    nExc -= stp;
                }
                if(nExc<=0)
                    break;
            }
            j += 1;
            if(j>58)
                break;
        }
    }
	if(low_range==0 && hgh_range==0)
		nPnt = 0;
	else
	{
		if(low_range==0 || hgh_range==0)
		{
			nPnt = 1;
			mRng = (hgh_range>low_range ? hgh_range : low_range); //max
		}
		else if(low_range+hgh_range>=54)
		{
			nPnt = 1;
			mRng = (hgh_range<low_range ? hgh_range : low_range); //min
		}
		else
			nPnt = 2;
	}
	if(nPnt==0 && low_alpha>=16 && low_alpha<=32)
	{
		sum = 0;
		for(i=5;i<=59;i++)
		{
			j = oHst[i]*(32-low_alpha)+tAvg*(low_alpha-16);
			j = (j+8)>>4;
			oHst[i] = j;
			sum += j;
			}
    	}
	else if(nPnt==1)
	{
		GetWgtLst(oHst+4, tAvg, mRng, low_alpha);
		GetWgtLst(oHst+4+mRng, tAvg, 54-mRng, hgh_alpha);
	}
	else if(nPnt==2)
	{
		mRng = 55-(low_range+hgh_range);
		GetWgtLst(oHst+4, tAvg, low_range, low_alpha);
		GetWgtLst(oHst+4+low_range, tAvg, mRng, mid_alpha);
		GetWgtLst(oHst+4+mRng+low_range, tAvg, hgh_range, hgh_alpha);
	}
	sum=0;
	for(i=4;i<=58;i++)
	{
		if(oHst[i]>cLmt)
			oHst[i] = cLmt;
		sum += oHst[i];
	}

	nStp = 0;
	//sum -= oHst[4];
	for(i=5;i<=59;i++)//5,59
	{
		nStp += oHst[i-1];
		//nStp += oHst[i];

		j = (236-16)*nStp;
		j += (sum>>1);
		j /= sum;

		ve_dnlp_tgt[i] = j + 16;
	}
	if(cnt)
	{
	for(i=0;i<64;i++)
		printk(" ve_dnlp_tgtx[%ld]=%d\n",i,ve_dnlp_tgt[i]);
	WRITE_CBUS_REG(ASSIST_SPARE8_REG1, 0);
	}
	return;
}

static void ve_dnlp_calculate_tgt(vframe_t *vf)
{
    struct vframe_prop_s *p = &vf->prop;
    ulong data[5];
	static unsigned int sum_b = 0, sum_c = 0;
    ulong i = 0, j = 0, ave = 0, max = 0, div = 0;
	unsigned int cnt = READ_CBUS_REG(ASSIST_SPARE8_REG1);

    // old historic luma sum
    sum_b = sum_c;
    sum_c = ve_dnlp_luma_sum;
    // new historic luma sum
    ve_dnlp_luma_sum = p->hist.luma_sum;
	if(dnlp_debug)
	printk("ve_dnlp_luma_sum=%x,sum_b=%x,sum_c=%x\n",ve_dnlp_luma_sum,sum_b,sum_c);

    // picture mode: freeze dnlp curve
    if(dnlp_respond)
    {
		if (// new luma sum is 0, something is wrong, freeze dnlp curve
			(!ve_dnlp_luma_sum)
		   )
	    return;
    }
	else
	{
	    if (// new luma sum is 0, something is wrong, freeze dnlp curve
	        (!ve_dnlp_luma_sum) ||
	        // new luma sum is closed to old one (1 +/- 1/64), picture mode, freeze curve
	        ((ve_dnlp_luma_sum < sum_b + (sum_b >> dnlp_adj_level)) &&//5
	         (ve_dnlp_luma_sum > sum_b - (sum_b >> dnlp_adj_level)) //5
	        )
	       )
	    return;
	}

    // get 5 regions
    for (i = 0; i < 5; i++)
    {
        j = 4 + 11 * i;
        data[i] = (ulong)p->hist.gamma[j     ] +
                  (ulong)p->hist.gamma[j +  1] +
                  (ulong)p->hist.gamma[j +  2] +
                  (ulong)p->hist.gamma[j +  3] +
                  (ulong)p->hist.gamma[j +  4] +
                  (ulong)p->hist.gamma[j +  5] +
                  (ulong)p->hist.gamma[j +  6] +
                  (ulong)p->hist.gamma[j +  7] +
                  (ulong)p->hist.gamma[j +  8] +
                  (ulong)p->hist.gamma[j +  9] +
                  (ulong)p->hist.gamma[j + 10];
    }

    // get max, ave, div
    for (i = 0; i < 5; i++)
    {
        if (max < data[i])
            max = data[i];
        ave += data[i];
        data[i] *= 5;
    }
    max *= 5;
    div = (max - ave > ave) ? max - ave : ave;

    // invalid histgram: freeze dnlp curve
    if (!max)
        return;

    // get 1st 4 points
    for (i = 0; i < 4; i++)
    {
        if (data[i] > ave)
            data[i] = 64 + (((data[i] - ave) << 1) + div) * ve_dnlp_rl / (div << 1);
        else if (data[i] < ave)
            data[i] = 64 - (((ave - data[i]) << 1) + div) * ve_dnlp_rl / (div << 1);
        else
            data[i] = 64;
        ve_dnlp_tgt[4 + 11 * (i + 1)] = ve_dnlp_tgt[4 + 11 * i] +
                                        ((44 * data[i] + 32) >> 6);
    }

    // fill in region 0 with black extension
    data[0] = ve_dnlp_black;
    if (data[0] > 16)
        data[0] = 16;
    data[0] = (ve_dnlp_tgt[15] - ve_dnlp_tgt[4]) * (16 - data[0]);
    for (j = 1; j <= 6; j++)
        ve_dnlp_tgt[4 + j] = ve_dnlp_tgt[4] + (data[0] * j + 88) / 176;
    data[0] = (ve_dnlp_tgt[15] - ve_dnlp_tgt[10]) << 1;
    for (j = 1; j <=4; j++)
        ve_dnlp_tgt[10 + j] = ve_dnlp_tgt[10] + (data[0] * j + 5) / 10;

    // fill in regions 1~3
    for (i = 1; i <= 3; i++)
    {
        data[i] = (ve_dnlp_tgt[11 * i + 15] - ve_dnlp_tgt[11 * i + 4]) << 1;
        for (j = 1; j <= 10; j++)
            ve_dnlp_tgt[11 * i + 4 + j] = ve_dnlp_tgt[11 * i + 4] + (data[i] * j + 11) / 22;
    }

    // fill in region 4 with white extension
    data[4] /= 20;
    data[4] = (ve_dnlp_white * ((ave << 4) - data[4] * ve_dnlp_white_factor)  + (ave << 3)) / (ave << 4);
    if (data[4] > 16)
        data[4] = 16;
    data[4] = (ve_dnlp_tgt[59] - ve_dnlp_tgt[48]) * (16 - data[4]);
    for (j = 1; j <= 6; j++)
        ve_dnlp_tgt[59 - j] = ve_dnlp_tgt[59] - (data[4] * j + 88) / 176;
    data[4] = (ve_dnlp_tgt[53] - ve_dnlp_tgt[48]) << 1;
    for (j = 1; j <= 4; j++)
        ve_dnlp_tgt[53 - j] = ve_dnlp_tgt[53] - (data[4] * j + 5) / 10;
	if(cnt)
	{
	for(i=0;i<64;i++)
		printk(" ve_dnlp_tgt[%ld]=%d\n",i,ve_dnlp_tgt[i]);
	WRITE_CBUS_REG(ASSIST_SPARE8_REG1, 0);
	}

}

static void ve_dnlp_calculate_lpf(void) // lpf[0] is always 0 & no need calculation
{
    ulong i = 0;

    for (i = 0; i < 64; i++) {
        ve_dnlp_lpf[i] = ve_dnlp_lpf[i] - (ve_dnlp_lpf[i] >> ve_dnlp_rt) + ve_dnlp_tgt[i];
    }
}

static void ve_dnlp_calculate_reg(void)
{
    ulong i = 0, j = 0, cur = 0, data = 0, offset = ve_dnlp_rt ? (1 << (ve_dnlp_rt - 1)) : 0;

    for (i = 0; i < 16; i++)
    {
        ve_dnlp_reg[i] = 0;
        cur = i << 2;
        for (j = 0; j < 4; j++)
        {
            data = (ve_dnlp_lpf[cur + j] + offset) >> ve_dnlp_rt;
            if (data > 255)
                data = 255;
            ve_dnlp_reg[i] |= data << (j << 3);
        }
    }
}

static void ve_dnlp_load_reg(void)
{
    WRITE_CBUS_REG(VPP_DNLP_CTRL_00, ve_dnlp_reg[0]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_01, ve_dnlp_reg[1]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_02, ve_dnlp_reg[2]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_03, ve_dnlp_reg[3]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_04, ve_dnlp_reg[4]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_05, ve_dnlp_reg[5]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_06, ve_dnlp_reg[6]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_07, ve_dnlp_reg[7]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_08, ve_dnlp_reg[8]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_09, ve_dnlp_reg[9]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_10, ve_dnlp_reg[10]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_11, ve_dnlp_reg[11]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_12, ve_dnlp_reg[12]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_13, ve_dnlp_reg[13]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_14, ve_dnlp_reg[14]);
    WRITE_CBUS_REG(VPP_DNLP_CTRL_15, ve_dnlp_reg[15]);
}
static unsigned int lock_range_50hz_fast =  7; // <= 14
static unsigned int lock_range_50hz_slow =  7; // <= 14
static unsigned int lock_range_60hz_fast =  5; // <=  4
static unsigned int lock_range_60hz_slow =  2; // <= 10
#define FLAG_LVDS_FREQ_SW1       (1 <<  6)

void ve_on_vs(vframe_t *vf)
{

    if (ve_en) {
        // calculate dnlp target data
        if(ve_dnlp_method == 0)
        ve_dnlp_calculate_tgt(vf);
		else if(ve_dnlp_method == 1)
		ve_dnlp_calculate_tgtx(vf);
		else if(ve_dnlp_method == 2)
		ve_dnlp_calculate_tgt_ext(vf);
		else
        ve_dnlp_calculate_tgt(vf);
        // calculate dnlp low-pass-filter data
        ve_dnlp_calculate_lpf();
        // calculate dnlp reg data
        ve_dnlp_calculate_reg();
        // load dnlp reg data
        ve_dnlp_load_reg();
    }
	ve_hist_gamma_tgt(vf);
    /* comment for duration algorithm is not based on panel vsync */
    if (vf->prop.meas.vs_cycle && !frame_lock_nosm)
    {
        if ((vecm_latch_flag & FLAG_LVDS_FREQ_SW1) &&
          (vf->duration >= 1920 - 19) &&
          (vf->duration <= 1920 + 19)
         )
            vpp_phase_lock_on_vs(vf->prop.meas.vs_cycle,
                                 vf->prop.meas.vs_stamp,
                                 true,
                                 lock_range_50hz_fast,
                                 lock_range_50hz_slow);
        if ((!(vecm_latch_flag & FLAG_LVDS_FREQ_SW1)) &&
          (vf->duration >= 1600 - 5) &&
          (vf->duration <= 1600 + 13)
         )
            vpp_phase_lock_on_vs(vf->prop.meas.vs_cycle,
                                 vf->prop.meas.vs_stamp,
                                 false,
                                 lock_range_60hz_fast,
                                 lock_range_60hz_slow);
    }

}
EXPORT_SYMBOL(ve_on_vs);

// ***************************************************************************
// *** IOCTL-oriented functions *********************************************
// ***************************************************************************

void vpp_enable_lcd_gamma_table(void)
{
    WRITE_MPEG_REG_BITS(L_GAMMA_CNTL_PORT, 1, GAMMA_EN, 1);
}

void vpp_disable_lcd_gamma_table(void)
{
    WRITE_MPEG_REG_BITS(L_GAMMA_CNTL_PORT, 0, GAMMA_EN, 1);
}

void vpp_set_lcd_gamma_table(u16 *data, u32 rgb_mask)
{
    int i;

    while (!(READ_MPEG_REG(L_GAMMA_CNTL_PORT) & (0x1 << ADR_RDY)));
    WRITE_MPEG_REG(L_GAMMA_ADDR_PORT, (0x1 << H_AUTO_INC) |
                                    (0x1 << rgb_mask)   |
                                    (0x0 << HADR));
    for (i=0;i<256;i++) {
        while (!( READ_MPEG_REG(L_GAMMA_CNTL_PORT) & (0x1 << WR_RDY) )) ;
        WRITE_MPEG_REG(L_GAMMA_DATA_PORT, data[i]);
    }
    while (!(READ_MPEG_REG(L_GAMMA_CNTL_PORT) & (0x1 << ADR_RDY)));
    WRITE_MPEG_REG(L_GAMMA_ADDR_PORT, (0x1 << H_AUTO_INC) |
                                    (0x1 << rgb_mask)   |
                                    (0x23 << HADR));
}

void vpp_set_rgb_ogo(struct tcon_rgb_ogo_s *p)
{

    // write to registers
    WRITE_CBUS_REG(VPP_GAINOFF_CTRL0, ((p->en            << 31) & 0x80000000) |
                                      ((p->r_gain        << 16) & 0x07ff0000) |
                                      ((p->g_gain        <<  0) & 0x000007ff));
    WRITE_CBUS_REG(VPP_GAINOFF_CTRL1, ((p->b_gain        << 16) & 0x07ff0000) |
                                      ((p->r_post_offset <<  0) & 0x000007ff));
    WRITE_CBUS_REG(VPP_GAINOFF_CTRL2, ((p->g_post_offset << 16) & 0x07ff0000) |
                                      ((p->b_post_offset <<  0) & 0x000007ff));
    WRITE_CBUS_REG(VPP_GAINOFF_CTRL3, ((p->r_pre_offset  << 16) & 0x07ff0000) |
                                      ((p->g_pre_offset  <<  0) & 0x000007ff));
    WRITE_CBUS_REG(VPP_GAINOFF_CTRL4, ((p->b_pre_offset  <<  0) & 0x000007ff));

}

void ve_enable_dnlp(void)
{
    ve_en = 1;
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, 1, DNLP_EN_BIT, DNLP_EN_WID);
}

void ve_disable_dnlp(void)
{
    ve_en = 0;
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, 0, DNLP_EN_BIT, DNLP_EN_WID);
}

void ve_set_dnlp(struct ve_dnlp_s *p)
{
    ulong i = 0;

    // get command parameters
    ve_en                = p->en;
    ve_dnlp_white_factor = (p->rt >> 4) & 0xf;
    ve_dnlp_rt           = p->rt & 0xf;
    ve_dnlp_rl           = p->rl;
    ve_dnlp_black        = p->black;
    ve_dnlp_white        = p->white;
    if (ve_en)
    {
        // clear historic luma sum
        ve_dnlp_luma_sum = 0;
        // init tgt & lpf
        for (i = 0; i < 64; i++) {
            ve_dnlp_tgt[i] = i << 2;
            ve_dnlp_lpf[i] = ve_dnlp_tgt[i] << ve_dnlp_rt;
        }
        // calculate dnlp reg data
        ve_dnlp_calculate_reg();
        // load dnlp reg data
        ve_dnlp_load_reg();
#ifdef NEW_DNLP_AFTER_PEAKING
        // enable dnlp
        WRITE_CBUS_REG_BITS(VPP_PEAKING_DNLP, 1, PEAKING_DNLP_EN_BIT, PEAKING_DNLP_EN_WID);
    }
    else
    {
        // disable dnlp
        WRITE_CBUS_REG_BITS(VPP_PEAKING_DNLP, 0, PEAKING_DNLP_EN_BIT, PEAKING_DNLP_EN_WID);
    }
#else
        // enable dnlp
        WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, 1, DNLP_EN_BIT, DNLP_EN_WID);
    }
    else
    {
        // disable dnlp
        WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, 0, DNLP_EN_BIT, DNLP_EN_WID);
    }
#endif
}
void ve_set_dnlp_2(void)
{
    ulong i = 0;

    // get command parameters
    ve_dnlp_method       = 1;
    ve_dnlp_cliprate     = 6;
    ve_dnlp_hghrange     = 14;
    ve_dnlp_lowrange     = 18;
    ve_dnlp_hghalpha     = 26;
    ve_dnlp_midalpha     = 28;
    ve_dnlp_lowalpha     = 18;

	// clear historic luma sum
	ve_dnlp_luma_sum = 0;
	// init tgt & lpf
	for (i = 0; i < 64; i++) {
	    ve_dnlp_tgt[i] = i << 2;
	    ve_dnlp_lpf[i] = ve_dnlp_tgt[i] << ve_dnlp_rt;
	}
	// calculate dnlp reg data
	ve_dnlp_calculate_reg();
	// load dnlp reg data
	ve_dnlp_load_reg();
}
void ve_set_new_dnlp(struct ve_dnlp_table_s *p)
{
    ulong i = 0;

    // get command parameters
    ve_en                = p->en;
	ve_dnlp_method       = p->method;
    ve_dnlp_cliprate     = p->cliprate;
    ve_dnlp_hghrange     = p->hghrange;
    ve_dnlp_lowrange     = p->lowrange;
    ve_dnlp_hghalpha     = p->hghalpha;
    ve_dnlp_midalpha     = p->midalpha;
    ve_dnlp_lowalpha     = p->lowalpha;

    if (ve_en)
    {
        // clear historic luma sum
        ve_dnlp_luma_sum = 0;
        // init tgt & lpf
        for (i = 0; i < 64; i++) {
            ve_dnlp_tgt[i] = i << 2;
            ve_dnlp_lpf[i] = ve_dnlp_tgt[i] << ve_dnlp_rt;
        }
        // calculate dnlp reg data
        ve_dnlp_calculate_reg();
        // load dnlp reg data
        ve_dnlp_load_reg();
#ifdef NEW_DNLP_AFTER_PEAKING
        // enable dnlp
        WRITE_CBUS_REG_BITS(VPP_PEAKING_DNLP, 1, PEAKING_DNLP_EN_BIT, PEAKING_DNLP_EN_WID);
    }
    else
    {
        // disable dnlp
        WRITE_CBUS_REG_BITS(VPP_PEAKING_DNLP, 0, PEAKING_DNLP_EN_BIT, PEAKING_DNLP_EN_WID);
    }
#else
        // enable dnlp
        WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, 1, DNLP_EN_BIT, DNLP_EN_WID);
    }
    else
    {
        // disable dnlp
        WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, 0, DNLP_EN_BIT, DNLP_EN_WID);
    }
#endif
}
unsigned int ve_get_vs_cnt(void)
{
    return (READ_CBUS_REG(VPP_VDO_MEAS_VS_COUNT_LO));
}

unsigned int vpp_log[128][10];

void vpp_phase_lock_on_vs(unsigned int cycle,
                          unsigned int stamp,
                          bool         lock50,
                          unsigned int range_fast,
                          unsigned int range_slow)
{
    unsigned int vtotal_ori = READ_CBUS_REG(ENCL_VIDEO_MAX_LNCNT);
    unsigned int vtotal     = lock50 ? 1349 : 1124;
	unsigned int stamp_in   = READ_CBUS_REG(VDIN_MEAS_VS_COUNT_LO);
    unsigned int stamp_out  = ve_get_vs_cnt();
    unsigned int phase      = 0;
	unsigned int cnt = READ_CBUS_REG(ASSIST_SPARE8_REG1);
	int step = 0, i = 0;

    // get phase
    if (stamp_out < stamp)
        phase = 0xffffffff - stamp + stamp_out + 1;
    else
        phase = stamp_out - stamp;
    while (phase >= cycle)
        phase -= cycle;
    // 225~315 degree => tune fast panel output
    if ((phase > ((cycle * 5) >> 3)) &&
        (phase < ((cycle * 7) >> 3))
       )
    {
        vtotal -= range_slow;
		step = 1;
    }
    // 45~135 degree => tune slow panel output
    else if ((phase > ( cycle      >> 3)) &&
             (phase < ((cycle * 3) >> 3))
            )
    {
        vtotal += range_slow;
		step = -1;
    }
    // 315~360 degree => tune fast panel output
    else if (phase >= ((cycle * 7) >> 3))
    {
        vtotal -= range_fast;
		step = +2;
    }
    // 0~45 degree => tune slow panel output
    else if (phase <= (cycle >> 3))
    {
        vtotal += range_fast;
		step = -2;
    }
    // 135~225 degree => keep still
    else
    {
        vtotal = vtotal_ori;
		step = 0;
    }
    if (vtotal != vtotal_ori)
        WRITE_CBUS_REG(ENCL_VIDEO_MAX_LNCNT, vtotal);
    if (cnt)
    {
        cnt--;
        WRITE_CBUS_REG(ASSIST_SPARE8_REG1, cnt);
        if (cnt)
        {
            vpp_log[cnt][0] = stamp;
            vpp_log[cnt][1] = stamp_in;
            vpp_log[cnt][2] = stamp_out;
            vpp_log[cnt][3] = cycle;
            vpp_log[cnt][4] = phase;
            vpp_log[cnt][5] = vtotal;
            vpp_log[cnt][6] = step;
        }
        else
        {
            for (i = 127; i > 0; i--)
            {
                printk("Ti=%10u Tio=%10u To=%10u CY=%6u PH =%10u Vt=%4u S=%2d\n",
                       vpp_log[i][0],
                       vpp_log[i][1],
                       vpp_log[i][2],
                       vpp_log[i][3],
                       vpp_log[i][4],
                       vpp_log[i][5],
                       vpp_log[i][6]
                       );
            }
            }
        }

}

