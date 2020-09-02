 /***************************************************************************\
|*                                                                           *|
|*       Copyright 1993-1999 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 1993-1999 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
 \***************************************************************************/

/*
 * GPL licensing note -- nVidia is allowing a liberal interpretation of
 * the documentation restriction above, to merely say that this nVidia's
 * copyright and disclaimer should be included with all code derived
 * from this source.  -- Jeff Garzik <jgarzik@pobox.com>, 01/Nov/99 
 */

/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/riva_hw.c,v 1.33 2002/08/05 20:47:06 mvojkovi Exp $ */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include "riva_hw.h"
#include "riva_tbl.h"
#include "nv_type.h"

/*
 * This file is an OS-agnostic file used to make RIVA 128 and RIVA TNT
 * operate identically (except TNT has more memory and better 3D quality.
 */
static int nv3Busy
(
    RIVA_HW_INST *chip
)
{
    return ((NV_RD32(&chip->Rop->FifoFree, 0) < chip->FifoEmptyCount) ||
	    NV_RD32(&chip->PGRAPH[0x000006B0/4], 0) & 0x01);
}
static int nv4Busy
(
    RIVA_HW_INST *chip
)
{
    return ((NV_RD32(&chip->Rop->FifoFree, 0) < chip->FifoEmptyCount) ||
	    NV_RD32(&chip->PGRAPH[0x00000700/4], 0) & 0x01);
}
static int nv10Busy
(
    RIVA_HW_INST *chip
)
{
    return ((NV_RD32(&chip->Rop->FifoFree, 0) < chip->FifoEmptyCount) ||
	    NV_RD32(&chip->PGRAPH[0x00000700/4], 0) & 0x01);
}

static void vgaLockUnlock
(
    RIVA_HW_INST *chip,
    int           Lock
)
{
    U008 cr11;
    VGA_WR08(chip->PCIO, 0x3D4, 0x11);
    cr11 = VGA_RD08(chip->PCIO, 0x3D5);
    if(Lock) cr11 |= 0x80;
    else cr11 &= ~0x80;
    VGA_WR08(chip->PCIO, 0x3D5, cr11);
}
static void nv3LockUnlock
(
    RIVA_HW_INST *chip,
    int           Lock
)
{
    VGA_WR08(chip->PVIO, 0x3C4, 0x06);
    VGA_WR08(chip->PVIO, 0x3C5, Lock ? 0x99 : 0x57);
    vgaLockUnlock(chip, Lock);
}
static void nv4LockUnlock
(
    RIVA_HW_INST *chip,
    int           Lock
)
{
    VGA_WR08(chip->PCIO, 0x3D4, 0x1F);
    VGA_WR08(chip->PCIO, 0x3D5, Lock ? 0x99 : 0x57);
    vgaLockUnlock(chip, Lock);
}

static int ShowHideCursor
(
    RIVA_HW_INST *chip,
    int           ShowHide
)
{
    int cursor;
    cursor                      =  chip->CurrentState->cursor1;
    chip->CurrentState->cursor1 = (chip->CurrentState->cursor1 & 0xFE) |
                                  (ShowHide & 0x01);
    VGA_WR08(chip->PCIO, 0x3D4, 0x31);
    VGA_WR08(chip->PCIO, 0x3D5, chip->CurrentState->cursor1);
    return (cursor & 0x01);
}

/****************************************************************************\
*                                                                            *
* The video arbitration routines calculate some "magic" numbers.  Fixes      *
* the snow seen when accessing the framebuffer without it.                   *
* It just works (I hope).                                                    *
*                                                                            *
\****************************************************************************/

#define DEFAULT_GR_LWM 100
#define DEFAULT_VID_LWM 100
#define DEFAULT_GR_BURST_SIZE 256
#define DEFAULT_VID_BURST_SIZE 128
#define VIDEO		0
#define GRAPHICS	1
#define MPORT		2
#define ENGINE		3
#define GFIFO_SIZE	320
#define GFIFO_SIZE_128	256
#define MFIFO_SIZE	120
#define VFIFO_SIZE	256

typedef struct {
  int gdrain_rate;
  int vdrain_rate;
  int mdrain_rate;
  int gburst_size;
  int vburst_size;
  char vid_en;
  char gr_en;
  int wcmocc, wcgocc, wcvocc, wcvlwm, wcglwm;
  int by_gfacc;
  char vid_only_once;
  char gr_only_once;
  char first_vacc;
  char first_gacc;
  char first_macc;
  int vocc;
  int gocc;
  int mocc;
  char cur;
  char engine_en;
  char converged;
  int priority;
} nv3_arb_info;
typedef struct {
  int graphics_lwm;
  int video_lwm;
  int graphics_burst_size;
  int video_burst_size;
  int graphics_hi_priority;
  int media_hi_priority;
  int rtl_values;
  int valid;
} nv3_fifo_info;
typedef struct {
  char pix_bpp;
  char enable_video;
  char gr_during_vid;
  char enable_mp;
  int memory_width;
  int video_scale;
  int pclk_khz;
  int mclk_khz;
  int mem_page_miss;
  int mem_latency;
  char mem_aligned;
} nv3_sim_state;
typedef struct {
  int graphics_lwm;
  int video_lwm;
  int graphics_burst_size;
  int video_burst_size;
  int valid;
} nv4_fifo_info;
typedef struct {
  int pclk_khz;
  int mclk_khz;
  int nvclk_khz;
  char mem_page_miss;
  char mem_latency;
  int memory_width;
  char enable_video;
  char gr_during_vid;
  char pix_bpp;
  char mem_aligned;
  char enable_mp;
} nv4_sim_state;
typedef struct {
  int graphics_lwm;
  int video_lwm;
  int graphics_burst_size;
  int video_burst_size;
  int valid;
} nv10_fifo_info;
typedef struct {
  int pclk_khz;
  int mclk_khz;
  int nvclk_khz;
  char mem_page_miss;
  char mem_latency;
  u32 memory_type;
  int memory_width;
  char enable_video;
  char gr_during_vid;
  char pix_bpp;
  char mem_aligned;
  char enable_mp;
} nv10_sim_state;
static int nv3_iterate(nv3_fifo_info *res_info, nv3_sim_state * state, nv3_arb_info *ainfo)
{
    int iter = 0;
    int tmp;
    int vfsize, mfsize, gfsize;
    int mburst_size = 32;
    int mmisses, gmisses, vmisses;
    int misses;
    int vlwm, glwm, mlwm;
    int last, next, cur;
    int max_gfsize ;
    long ns;

    vlwm = 0;
    glwm = 0;
    mlwm = 0;
    vfsize = 0;
    gfsize = 0;
    cur = ainfo->cur;
    mmisses = 2;
    gmisses = 2;
    vmisses = 2;
    if (ainfo->gburst_size == 128) max_gfsize = GFIFO_SIZE_128;
    else  max_gfsize = GFIFO_SIZE;
    max_gfsize = GFIFO_SIZE;
    while (1)
    {
        if (ainfo->vid_en)
        {
            if (ainfo->wcvocc > ainfo->vocc) ainfo->wcvocc = ainfo->vocc;
            if (ainfo->wcvlwm > vlwm) ainfo->wcvlwm = vlwm ;
            ns = 1000000 * ainfo->vburst_size/(state->memory_width/8)/state->mclk_khz;
            vfsize = ns * ainfo->vdrain_rate / 1000000;
            vfsize =  ainfo->wcvlwm - ainfo->vburst_size + vfsize;
        }
        if (state->enable_mp)
        {
            if (ainfo->wcmocc > ainfo->mocc) ainfo->wcmocc = ainfo->mocc;
        }
        if (ainfo->gr_en)
        {
            if (ainfo->wcglwm > glwm) ainfo->wcglwm = glwm ;
            if (ainfo->wcgocc > ainfo->gocc) ainfo->wcgocc = ainfo->gocc;
            ns = 1000000 * (ainfo->gburst_size/(state->memory_width/8))/state->mclk_khz;
            gfsize = (ns * (long) ainfo->gdrain_rate)/1000000;
            gfsize = ainfo->wcglwm - ainfo->gburst_size + gfsize;
        }
        mfsize = 0;
        if (!state->gr_during_vid && ainfo->vid_en)
            if (ainfo->vid_en && (ainfo->vocc < 0) && !ainfo->vid_only_once)
                next = VIDEO;
            else if (ainfo->mocc < 0)
                next = MPORT;
            else if (ainfo->gocc< ainfo->by_gfacc)
                next = GRAPHICS;
            else return (0);
        else switch (ainfo->priority)
            {
                case VIDEO:
                    if (ainfo->vid_en && ainfo->vocc<0 && !ainfo->vid_only_once)
                        next = VIDEO;
                    else if (ainfo->gr_en && ainfo->gocc<0 && !ainfo->gr_only_once)
                        next = GRAPHICS;
                    else if (ainfo->mocc<0)
                        next = MPORT;
                    else    return (0);
                    break;
                case GRAPHICS:
                    if (ainfo->gr_en && ainfo->gocc<0 && !ainfo->gr_only_once)
                        next = GRAPHICS;
                    else if (ainfo->vid_en && ainfo->vocc<0 && !ainfo->vid_only_once)
                        next = VIDEO;
                    else if (ainfo->mocc<0)
                        next = MPORT;
                    else    return (0);
                    break;
                default:
                    if (ainfo->mocc<0)
                        next = MPORT;
                    else if (ainfo->gr_en && ainfo->gocc<0 && !ainfo->gr_only_once)
                        next = GRAPHICS;
                    else if (ainfo->vid_en && ainfo->vocc<0 && !ainfo->vid_only_once)
                        next = VIDEO;
                    else    return (0);
                    break;
            }
        last = cur;
        cur = next;
        iter++;
        switch (cur)
        {
            case VIDEO:
                if (last==cur)    misses = 0;
                else if (ainfo->first_vacc)   misses = vmisses;
                else    misses = 1;
                ainfo->first_vacc = 0;
                if (last!=cur)
                {
                    ns =  1000000 * (vmisses*state->mem_page_miss + state->mem_latency)/state->mclk_khz; 
                    vlwm = ns * ainfo->vdrain_rate/ 1000000;
                    vlwm = ainfo->vocc - vlwm;
                }
                ns = 1000000*(misses*state->mem_page_miss + ainfo->vburst_size)/(state->memory_width/8)/state->mclk_khz;
                ainfo->vocc = ainfo->vocc + ainfo->vburst_size - ns*ainfo->vdrain_rate/1000000;
                ainfo->gocc = ainfo->gocc - ns*ainfo->gdrain_rate/1000000;
                ainfo->mocc = ainfo->mocc - ns*ainfo->mdrain_rate/1000000;
                break;
            case GRAPHICS:
                if (last==cur)    misses = 0;
                else if (ainfo->first_gacc)   misses = gmisses;
                else    misses = 1;
                ainfo->first_gacc = 0;
                if (last!=cur)
                {
                    ns = 1000000*(gmisses*state->mem_page_miss + state->mem_latency)/state->mclk_khz ;
                    glwm = ns * ainfo->gdrain_rate/1000000;
                    glwm = ainfo->gocc - glwm;
                }
                ns = 1000000*(misses*state->mem_page_miss + ainfo->gburst_size/(state->memory_width/8))/state->mclk_khz;
                ainfo->vocc = ainfo->vocc + 0 - ns*ainfo->vdrain_rate/1000000;
                ainfo->gocc = ainfo->gocc + ainfo->gburst_size - ns*ainfo->gdrain_rate/1000000;
                ainfo->mocc = ainfo->mocc + 0 - ns*ainfo->mdrain_rate/1000000;
                break;
            default:
                if (last==cur)    misses = 0;
                else if (ainfo->first_macc)   misses = mmisses;
                else    misses = 1;
                ainfo->first_macc = 0;
                ns = 1000000*(misses*state->mem_page_miss + mburst_size/(state->memory_width/8))/state->mclk_khz;
                ainfo->vocc = ainfo->vocc + 0 - ns*ainfo->vdrain_rate/1000000;
                ainfo->gocc = ainfo->gocc + 0 - ns*ainfo->gdrain_rate/1000000;
                ainfo->mocc = ainfo->mocc + mburst_size - ns*ainfo->mdrain_rate/1000000;
                break;
        }
        if (iter>100)
        {
            ainfo->converged = 0;
            return (1);
        }
        ns = 1000000*ainfo->gburst_size/(state->memory_width/8)/state->mclk_khz;
        tmp = ns * ainfo->gdrain_rate/1000000;
        if (abs(ainfo->gburst_size) + ((abs(ainfo->wcglwm) + 16 ) & ~0x7) - tmp > max_gfsize)
        {
            ainfo->converged = 0;
            return (1);
        }
        ns = 1000000*ainfo->vburst_size/(state->memory_width/8)/state->mclk_khz;
        tmp = ns * ainfo->vdrain_rate/1000000;
        if (abs(ainfo->vburst_size) + (abs(ainfo->wcvlwm + 32) & ~0xf)  - tmp> VFIFO_SIZE)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (abs(ainfo->gocc) > max_gfsize)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (abs(ainfo->vocc) > VFIFO_SIZE)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (abs(ainfo->mocc) > MFIFO_SIZE)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (abs(vfsize) > VFIFO_SIZE)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (abs(gfsize) > max_gfsize)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (abs(mfsize) > MFIFO_SIZE)
        {
            ainfo->converged = 0;
            return (1);
        }
    }
}
static char nv3_arb(nv3_fifo_info * res_info, nv3_sim_state * state,  nv3_arb_info *ainfo) 
{
    long ens, vns, mns, gns;
    int mmisses, gmisses, vmisses, eburst_size, mburst_size;
    int refresh_cycle;

    refresh_cycle = 2*(state->mclk_khz/state->pclk_khz) + 5;
    mmisses = 2;
    if (state->mem_aligned) gmisses = 2;
    else    gmisses = 3;
    vmisses = 2;
    eburst_size = state->memory_width * 1;
    mburst_size = 32;
    gns = 1000000 * (gmisses*state->mem_page_miss + state->mem_latency)/state->mclk_khz;
    ainfo->by_gfacc = gns*ainfo->gdrain_rate/1000000;
    ainfo->wcmocc = 0;
    ainfo->wcgocc = 0;
    ainfo->wcvocc = 0;
    ainfo->wcvlwm = 0;
    ainfo->wcglwm = 0;
    ainfo->engine_en = 1;
    ainfo->converged = 1;
    if (ainfo->engine_en)
    {
        ens =  1000000*(state->mem_page_miss + eburst_size/(state->memory_width/8) +refresh_cycle)/state->mclk_khz;
        ainfo->mocc = state->enable_mp ? 0-ens*ainfo->mdrain_rate/1000000 : 0;
        ainfo->vocc = ainfo->vid_en ? 0-ens*ainfo->vdrain_rate/1000000 : 0;
        ainfo->gocc = ainfo->gr_en ? 0-ens*ainfo->gdrain_rate/1000000 : 0;
        ainfo->cur = ENGINE;
        ainfo->first_vacc = 1;
        ainfo->first_gacc = 1;
        ainfo->first_macc = 1;
        nv3_iterate(res_info, state,ainfo);
    }
    if (state->enable_mp)
    {
        mns = 1000000 * (mmisses*state->mem_page_miss + mburst_size/(state->memory_width/8) + refresh_cycle)/state->mclk_khz;
        ainfo->mocc = state->enable_mp ? 0 : mburst_size - mns*ainfo->mdrain_rate/1000000;
        ainfo->vocc = ainfo->vid_en ? 0 : 0- mns*ainfo->vdrain_rate/1000000;
        ainfo->gocc = ainfo->gr_en ? 0: 0- mns*ainfo->gdrain_rate/1000000;
        ainfo->cur = MPORT;
        ainfo->first_vacc = 1;
        ainfo->first_gacc = 1;
        ainfo->first_macc = 0;
        nv3_iterate(res_info, state,ainfo);
    }
    if (ainfo->gr_en)
    {
        ainfo->first_vacc = 1;
        ainfo->first_gacc = 0;
        ainfo->first_macc = 1;
        gns = 1000000*(gmisses*state->mem_page_miss + ainfo->gburst_size/(state->memory_width/8) + refresh_cycle)/state->mclk_khz;
        ainfo->gocc = ainfo->gburst_size - gns*ainfo->gdrain_rate/1000000;
        ainfo->vocc = ainfo->vid_en? 0-gns*ainfo->vdrain_rate/1000000 : 0;
        ainfo->mocc = state->enable_mp ?  0-gns*ainfo->mdrain_rate/1000000: 0;
        ainfo->cur = GRAPHICS;
        nv3_iterate(res_info, state,ainfo);
    }
    if (ainfo->vid_en)
    {
        ainfo->first_vacc = 0;
        ainfo->first_gacc = 1;
        ainfo->first_macc = 1;
        vns = 1000000*(vmisses*state->mem_page_miss + ainfo->vburst_size/(state->memory_width/8) + refresh_cycle)/state->mclk_khz;
        ainfo->vocc = ainfo->vburst_size - vns*ainfo->vdrain_rate/1000000;
        ainfo->gocc = ainfo->gr_en? (0-vns*ainfo->gdrain_rate/1000000) : 0;
        ainfo->mocc = state->enable_mp? 0-vns*ainfo->mdrain_rate/1000000 :0 ;
        ainfo->cur = VIDEO;
        nv3_iterate(res_info, state, ainfo);
    }
    if (ainfo->converged)
    {
        res_info->graphics_lwm = (int)abs(ainfo->wcglwm) + 16;
        res_info->video_lwm = (int)abs(ainfo->wcvlwm) + 32;
        res_info->graphics_burst_size = ainfo->gburst_size;
        res_info->video_burst_size = ainfo->vburst_size;
        res_info->graphics_hi_priority = (ainfo->priority == GRAPHICS);
        res_info->media_hi_priority = (ainfo->priority == MPORT);
        if (res_info->video_lwm > 160)
        {
            res_info->graphics_lwm = 256;
            res_info->video_lwm = 128;
            res_info->graphics_burst_size = 64;
            res_info->video_burst_size = 64;
            res_info->graphics_hi_priority = 0;
            res_info->media_hi_priority = 0;
            ainfo->converged = 0;
            return (0);
        }
        if (res_info->video_lwm > 128)
        {
            res_info->video_lwm = 128;
        }
        return (1);
    }
    else
    {
        res_info->graphics_lwm = 256;
        res_info->video_lwm = 128;
        res_info->graphics_burst_size = 64;
        res_info->video_burst_size = 64;
        res_info->graphics_hi_priority = 0;
        res_info->media_hi_priority = 0;
        return (0);
    }
}
static char nv3_get_param(nv3_fifo_info *res_info, nv3_sim_state * state, nv3_arb_info *ainfo)
{
    int done, g,v, p;
    
    done = 0;
    for (p=0; p < 2; p++)
    {
        for (g=128 ; g > 32; g= g>> 1)
        {
            for (v=128; v >=32; v = v>> 1)
            {
                ainfo->priority = p;
                ainfo->gburst_size = g;     
                ainfo->vburst_size = v;
                done = nv3_arb(res_info, state,ainfo);
                if (done && (g==128))
                    if ((res_info->graphics_lwm + g) > 256)
                        done = 0;
                if (done)
                    goto Done;
            }
        }
    }

 Done:
    return done;
}
static void nv3CalcArbitration 
(
    nv3_fifo_info * res_info,
    nv3_sim_state * state
)
{
    nv3_fifo_info save_info;
    nv3_arb_info ainfo;
    char   res_gr, res_vid;

    ainfo.gr_en = 1;
    ainfo.vid_en = state->enable_video;
    ainfo.vid_only_once = 0;
    ainfo.gr_only_once = 0;
    ainfo.gdrain_rate = (int) state->pclk_khz * (state->pix_bpp/8);
    ainfo.vdrain_rate = (int) state->pclk_khz * 2;
    if (state->video_scale != 0)
        ainfo.vdrain_rate = ainfo.vdrain_rate/state->video_scale;
    ainfo.mdrain_rate = 33000;
    res_info->rtl_values = 0;
    if (!state->gr_during_vid && state->enable_video)
    {
        ainfo.gr_only_once = 1;
        ainfo.gr_en = 1;
        ainfo.gdrain_rate = 0;
        res_vid = nv3_get_param(res_info, state,  &ainfo);
        res_vid = ainfo.converged;
        save_info.video_lwm = res_info->video_lwm;
        save_info.video_burst_size = res_info->video_burst_size;
        ainfo.vid_en = 1;
        ainfo.vid_only_once = 1;
        ainfo.gr_en = 1;
        ainfo.gdrain_rate = (int) state->pclk_khz * (state->pix_bpp/8);
        ainfo.vdrain_rate = 0;
        res_gr = nv3_get_param(res_info, state,  &ainfo);
        res_gr = ainfo.converged;
        res_info->video_lwm = save_info.video_lwm;
        res_info->video_burst_size = save_info.video_burst_size;
        res_info->valid = res_gr & res_vid;
    }
    else
    {
        if (!ainfo.gr_en) ainfo.gdrain_rate = 0;
        if (!ainfo.vid_en) ainfo.vdrain_rate = 0;
        res_gr = nv3_get_param(res_info, state,  &ainfo);
        res_info->valid = ainfo.converged;
    }
}
static void nv3UpdateArbitrationSettings
(
    unsigned      VClk, 
    unsigned      pixelDepth, 
    unsigned     *burst,
    unsigned     *lwm,
    RIVA_HW_INST *chip
)
{
    nv3_fifo_info fifo_data;
    nv3_sim_state sim_data;
    unsigned int M, N, P, pll, MClk;
    
    pll = NV_RD32(&chip->PRAMDAC0[0x00000504/4], 0);
    M = (pll >> 0) & 0xFF; N = (pll >> 8) & 0xFF; P = (pll >> 16) & 0x0F;
    MClk = (N * chip->CrystalFreqKHz / M) >> P;
    sim_data.pix_bpp        = (char)pixelDepth;
    sim_data.enable_video   = 0;
    sim_data.enable_mp      = 0;
    sim_data.video_scale    = 1;
    sim_data.memory_width   = (NV_RD32(&chip->PEXTDEV[0x00000000/4], 0) & 0x10) ?
	128 : 64;
    sim_data.memory_width   = 128;

    sim_data.mem_latency    = 9;
    sim_data.mem_aligned    = 1;
    sim_data.mem_page_miss  = 11;
    sim_data.gr_during_vid  = 0;
    sim_data.pclk_khz       = VClk;
    sim_data.mclk_khz       = MClk;
    nv3CalcArbitration(&fifo_data, &sim_data);
    if (fifo_data.valid)
    {
        int  b = fifo_data.graphics_burst_size >> 4;
        *burst = 0;
        while (b >>= 1)
	    (*burst)++;
        *lwm   = fifo_data.graphics_lwm >> 3;
    }
    else
    {
        *lwm   = 0x24;
        *burst = 0x2;
    }
}
static void nv4CalcArbitration 
(
    nv4_fifo_info *fifo,
    nv4_sim_state *arb
)
{
    int data, pagemiss, cas,width, video_enable, color_key_enable, bpp, align;
    int nvclks, mclks, pclks, vpagemiss, crtpagemiss, vbs;
    int found, mclk_extra, mclk_loop, cbs, m1, p1;
    int mclk_freq, pclk_freq, nvclk_freq, mp_enable;
    int us_m, us_n, us_p, video_drain_rate, crtc_drain_rate;
    int vpm_us, us_video, vlwm, video_fill_us, cpm_us, us_crt,clwm;
    int craw, vraw;

    fifo->valid = 1;
    pclk_freq = arb->pclk_khz;
    mclk_freq = arb->mclk_khz;
    nvclk_freq = arb->nvclk_khz;
    pagemiss = arb->mem_page_miss;
    cas = arb->mem_latency;
    width = arb->memory_width >> 6;
    video_enable = arb->enable_video;
    color_key_enable = arb->gr_during_vid;
    bpp = arb->pix_bpp;
    align = arb->mem_aligned;
    mp_enable = arb->enable_mp;
    clwm = 0;
    vlwm = 0;
    cbs = 128;
    pclks = 2;
    nvclks = 2;
    nvclks += 2;
    nvclks += 1;
    mclks = 5;
    mclks += 3;
    mclks += 1;
    mclks += cas;
    mclks += 1;
    mclks += 1;
    mclks += 1;
    mclks += 1;
    mclk_extra = 3;
    nvclks += 2;
    nvclks += 1;
    nvclks += 1;
    nvclks += 1;
    if (mp_enable)
        mclks+=4;
    nvclks += 0;
    pclks += 0;
    found = 0;
    vbs = 0;
    while (found != 1)
    {
        fifo->valid = 1;
        found = 1;
        mclk_loop = mclks+mclk_extra;
        us_m = mclk_loop *1000*1000 / mclk_freq;
        us_n = nvclks*1000*1000 / nvclk_freq;
        us_p = nvclks*1000*1000 / pclk_freq;
        if (video_enable)
        {
            video_drain_rate = pclk_freq * 2;
            crtc_drain_rate = pclk_freq * bpp/8;
            vpagemiss = 2;
            vpagemiss += 1;
            crtpagemiss = 2;
            vpm_us = (vpagemiss * pagemiss)*1000*1000/mclk_freq;
            if (nvclk_freq * 2 > mclk_freq * width)
                video_fill_us = cbs*1000*1000 / 16 / nvclk_freq ;
            else
                video_fill_us = cbs*1000*1000 / (8 * width) / mclk_freq;
            us_video = vpm_us + us_m + us_n + us_p + video_fill_us;
            vlwm = us_video * video_drain_rate/(1000*1000);
            vlwm++;
            vbs = 128;
            if (vlwm > 128) vbs = 64;
            if (vlwm > (256-64)) vbs = 32;
            if (nvclk_freq * 2 > mclk_freq * width)
                video_fill_us = vbs *1000*1000/ 16 / nvclk_freq ;
            else
                video_fill_us = vbs*1000*1000 / (8 * width) / mclk_freq;
            cpm_us = crtpagemiss  * pagemiss *1000*1000/ mclk_freq;
            us_crt =
            us_video
            +video_fill_us
            +cpm_us
            +us_m + us_n +us_p
            ;
            clwm = us_crt * crtc_drain_rate/(1000*1000);
            clwm++;
        }
        else
        {
            crtc_drain_rate = pclk_freq * bpp/8;
            crtpagemiss = 2;
            crtpagemiss += 1;
            cpm_us = crtpagemiss  * pagemiss *1000*1000/ mclk_freq;
            us_crt =  cpm_us + us_m + us_n + us_p ;
            clwm = us_crt * crtc_drain_rate/(1000*1000);
            clwm++;
        }
        m1 = clwm + cbs - 512;
        p1 = m1 * pclk_freq / mclk_freq;
        p1 = p1 * bpp / 8;
        if ((p1 < m1) && (m1 > 0))
        {
            fifo->valid = 0;
            found = 0;
            if (mclk_extra ==0)   found = 1;
            mclk_extra--;
        }
        else if (video_enable)
        {
            if ((clwm > 511) || (vlwm > 255))
            {
                fifo->valid = 0;
                found = 0;
                if (mclk_extra ==0)   found = 1;
                mclk_extra--;
            }
        }
        else
        {
            if (clwm > 519)
            {
                fifo->valid = 0;
                found = 0;
                if (mclk_extra ==0)   found = 1;
                mclk_extra--;
            }
        }
        craw = clwm;
        vraw = vlwm;
        if (clwm < 384) clwm = 384;
        if (vlwm < 128) vlwm = 128;
        data = (int)(clwm);
        fifo->graphics_lwm = data;
        fifo->graphics_burst_size = 128;
        data = (int)((vlwm+15));
        fifo->video_lwm = data;
        fifo->video_burst_size = vbs;
    }
}
static void nv4UpdateArbitrationSettings
(
    unsigned      VClk, 
    unsigned      pixelDepth, 
    unsigned     *burst,
    unsigned     *lwm,
    RIVA_HW_INST *chip
)
{
    nv4_fifo_info fifo_data;
    nv4_sim_state sim_data;
    unsigned int M, N, P, pll, MClk, NVClk, cfg1;

    pll = NV_RD32(&chip->PRAMDAC0[0x00000504/4], 0);
    M = (pll >> 0)  & 0xFF; N = (pll >> 8)  & 0xFF; P = (pll >> 16) & 0x0F;
    MClk  = (N * chip->CrystalFreqKHz / M) >> P;
    pll = NV_RD32(&chip->PRAMDAC0[0x00000500/4], 0);
    M = (pll >> 0)  & 0xFF; N = (pll >> 8)  & 0xFF; P = (pll >> 16) & 0x0F;
    NVClk  = (N * chip->CrystalFreqKHz / M) >> P;
    cfg1 = NV_RD32(&chip->PFB[0x00000204/4], 0);
    sim_data.pix_bpp        = (char)pixelDepth;
    sim_data.enable_video   = 0;
    sim_data.enable_mp      = 0;
    sim_data.memory_width   = (NV_RD32(&chip->PEXTDEV[0x00000000/4], 0) & 0x10) ?
	128 : 64;
    sim_data.mem_latency    = (char)cfg1 & 0x0F;
    sim_data.mem_aligned    = 1;
    sim_data.mem_page_miss  = (char)(((cfg1 >> 4) &0x0F) + ((cfg1 >> 31) & 0x01));
    sim_data.gr_during_vid  = 0;
    sim_data.pclk_khz       = VClk;
    sim_data.mclk_khz       = MClk;
    sim_data.nvclk_khz      = NVClk;
    nv4CalcArbitration(&fifo_data, &sim_data);
    if (fifo_data.valid)
    {
        int  b = fifo_data.graphics_burst_size >> 4;
        *burst = 0;
        while (b >>= 1)
	    (*burst)++;
        *lwm   = fifo_data.graphics_lwm >> 3;
    }
}
static void nv10CalcArbitration 
(
    nv10_fifo_info *fifo,
    nv10_sim_state *arb
)
{
    int data, pagemiss, cas,width, video_enable, color_key_enable, bpp, align;
    int nvclks, mclks, pclks, vpagemiss, crtpagemiss, vbs;
    int nvclk_fill, us_extra;
    int found, mclk_extra, mclk_loop, cbs, m1;
    int mclk_freq, pclk_freq, nvclk_freq, mp_enable;
    int us_m, us_m_min, us_n, us_p, video_drain_rate, crtc_drain_rate;
    int vus_m, vus_n, vus_p;
    int vpm_us, us_video, vlwm, cpm_us, us_crt,clwm;
    int clwm_rnd_down;
    int craw, m2us, us_pipe, us_pipe_min, vus_pipe, p1clk, p2;
    int pclks_2_top_fifo, min_mclk_extra;
    int us_min_mclk_extra;

    fifo->valid = 1;
    pclk_freq = arb->pclk_khz; /* freq in KHz */
    mclk_freq = arb->mclk_khz;
    nvclk_freq = arb->nvclk_khz;
    pagemiss = arb->mem_page_miss;
    cas = arb->mem_latency;
    width = arb->memory_width/64;
    video_enable = arb->enable_video;
    color_key_enable = arb->gr_during_vid;
    bpp = arb->pix_bpp;
    align = arb->mem_aligned;
    mp_enable = arb->enable_mp;
    clwm = 0;
    vlwm = 1024;

    cbs = 512;
    vbs = 512;

    pclks = 4; /* lwm detect. */

    nvclks = 3; /* lwm -> sync. */
    nvclks += 2; /* fbi bus cycles (1 req + 1 busy) */

    mclks  = 1;   /* 2 edge sync.  may be very close to edge so just put one. */

    mclks += 1;   /* arb_hp_req */
    mclks += 5;   /* ap_hp_req   tiling pipeline */

    mclks += 2;    /* tc_req     latency fifo */
    mclks += 2;    /* fb_cas_n_  memory request to fbio block */
    mclks += 7;    /* sm_d_rdv   data returned from fbio block */

    /* fb.rd.d.Put_gc   need to accumulate 256 bits for read */
    if (arb->memory_type == 0)
      if (arb->memory_width == 64) /* 64 bit bus */
        mclks += 4;
      else
        mclks += 2;
    else
      if (arb->memory_width == 64) /* 64 bit bus */
        mclks += 2;
      else
        mclks += 1;

    if ((!video_enable) && (arb->memory_width == 128))
    {  
      mclk_extra = (bpp == 32) ? 31 : 42; /* Margin of error */
      min_mclk_extra = 17;
    }
    else
    {
      mclk_extra = (bpp == 32) ? 8 : 4; /* Margin of error */
      /* mclk_extra = 4; */ /* Margin of error */
      min_mclk_extra = 18;
    }

    nvclks += 1; /* 2 edge sync.  may be very close to edge so just put one. */
    nvclks += 1; /* fbi_d_rdv_n */
    nvclks += 1; /* Fbi_d_rdata */
    nvclks += 1; /* crtfifo load */

    if(mp_enable)
      mclks+=4; /* Mp can get in with a burst of 8. */
    /* Extra clocks determined by heuristics */

    nvclks += 0;
    pclks += 0;
    found = 0;
    while(found != 1) {
      fifo->valid = 1;
      found = 1;
      mclk_loop = mclks+mclk_extra;
      us_m = mclk_loop *1000*1000 / mclk_freq; /* Mclk latency in us */
      us_m_min = mclks * 1000*1000 / mclk_freq; /* Minimum Mclk latency in us */
      us_min_mclk_extra = min_mclk_extra *1000*1000 / mclk_freq;
      us_n = nvclks*1000*1000 / nvclk_freq;/* nvclk latency in us */
      us_p = pclks*1000*1000 / pclk_freq;/* nvclk latency in us */
      us_pipe = us_m + us_n + us_p;
      us_pipe_min = us_m_min + us_n + us_p;
      us_extra = 0;

      vus_m = mclk_loop *1000*1000 / mclk_freq; /* Mclk latency in us */
      vus_n = (4)*1000*1000 / nvclk_freq;/* nvclk latency in us */
      vus_p = 0*1000*1000 / pclk_freq;/* pclk latency in us */
      vus_pipe = vus_m + vus_n + vus_p;

      if(video_enable) {
        video_drain_rate = pclk_freq * 4; /* MB/s */
        crtc_drain_rate = pclk_freq * bpp/8; /* MB/s */

        vpagemiss = 1; /* self generating page miss */
        vpagemiss += 1; /* One higher priority before */

        crtpagemiss = 2; /* self generating page miss */
        if(mp_enable)
            crtpagemiss += 1; /* if MA0 conflict */

        vpm_us = (vpagemiss * pagemiss)*1000*1000/mclk_freq;

        us_video = vpm_us + vus_m; /* Video has separate read return path */

        cpm_us = crtpagemiss  * pagemiss *1000*1000/ mclk_freq;
        us_crt =
          us_video  /* Wait for video */
          +cpm_us /* CRT Page miss */
          +us_m + us_n +us_p /* other latency */
          ;

        clwm = us_crt * crtc_drain_rate/(1000*1000);
        clwm++; /* fixed point <= float_point - 1.  Fixes that */
      } else {
        crtc_drain_rate = pclk_freq * bpp/8; /* bpp * pclk/8 */

        crtpagemiss = 1; /* self generating page miss */
        crtpagemiss += 1; /* MA0 page miss */
        if(mp_enable)
            crtpagemiss += 1; /* if MA0 conflict */
        cpm_us = crtpagemiss  * pagemiss *1000*1000/ mclk_freq;
        us_crt =  cpm_us + us_m + us_n + us_p ;
        clwm = us_crt * crtc_drain_rate/(1000*1000);
        clwm++; /* fixed point <= float_point - 1.  Fixes that */

  /*
          //
          // Another concern, only for high pclks so don't do this
          // with video:
          // What happens if the latency to fetch the cbs is so large that
          // fifo empties.  In that case we need to have an alternate clwm value
          // based off the total burst fetch
          //
          us_crt = (cbs * 1000 * 1000)/ (8*width)/mclk_freq ;
          us_crt = us_crt + us_m + us_n + us_p + (4 * 1000 * 1000)/mclk_freq;
          clwm_mt = us_crt * crtc_drain_rate/(1000*1000);
          clwm_mt ++;
          if(clwm_mt > clwm)
              clwm = clwm_mt;
  */
          /* Finally, a heuristic check when width == 64 bits */
          if(width == 1){
              nvclk_fill = nvclk_freq * 8;
              if(crtc_drain_rate * 100 >= nvclk_fill * 102)
                      clwm = 0xfff; /*Large number to fail */

              else if(crtc_drain_rate * 100  >= nvclk_fill * 98) {
                  clwm = 1024;
                  cbs = 512;
                  us_extra = (cbs * 1000 * 1000)/ (8*width)/mclk_freq ;
              }
          }
      }


      /*
        Overfill check:

        */

      clwm_rnd_down = ((int)clwm/8)*8;
      if (clwm_rnd_down < clwm)
          clwm += 8;

      m1 = clwm + cbs -  1024; /* Amount of overfill */
      m2us = us_pipe_min + us_min_mclk_extra;
      pclks_2_top_fifo = (1024-clwm)/(8*width);

      /* pclk cycles to drain */
      p1clk = m2us * pclk_freq/(1000*1000); 
      p2 = p1clk * bpp / 8; /* bytes drained. */

      if((p2 < m1) && (m1 > 0)) {
          fifo->valid = 0;
          found = 0;
          if(min_mclk_extra == 0)   {
            if(cbs <= 32) {
              found = 1; /* Can't adjust anymore! */
            } else {
              cbs = cbs/2;  /* reduce the burst size */
            }
          } else {
            min_mclk_extra--;
          }
      } else {
        if (clwm > 1023){ /* Have some margin */
          fifo->valid = 0;
          found = 0;
          if(min_mclk_extra == 0)   
              found = 1; /* Can't adjust anymore! */
          else 
              min_mclk_extra--;
        }
      }
      craw = clwm;

      if(clwm < (1024-cbs+8)) clwm = 1024-cbs+8;
      data = (int)(clwm);
      /*  printf("CRT LWM: %f bytes, prog: 0x%x, bs: 256\n", clwm, data ); */
      fifo->graphics_lwm = data;   fifo->graphics_burst_size = cbs;

      /*  printf("VID LWM: %f bytes, prog: 0x%x, bs: %d\n, ", vlwm, data, vbs ); */
      fifo->video_lwm = 1024;  fifo->video_burst_size = 512;
    }
}
static void nv10UpdateArbitrationSettings
(
    unsigned      VClk, 
    unsigned      pixelDepth, 
    unsigned     *burst,
    unsigned     *lwm,
    RIVA_HW_INST *chip
)
{
    nv10_fifo_info fifo_data;
    nv10_sim_state sim_data;
    unsigned int M, N, P, pll, MClk, NVClk, cfg1;

    pll = NV_RD32(&chip->PRAMDAC0[0x00000504/4], 0);
    M = (pll >> 0)  & 0xFF; N = (pll >> 8)  & 0xFF; P = (pll >> 16) & 0x0F;
    MClk  = (N * chip->CrystalFreqKHz / M) >> P;
    pll = NV_RD32(&chip->PRAMDAC0[0x00000500/4], 0);
    M = (pll >> 0)  & 0xFF; N = (pll >> 8)  & 0xFF; P = (pll >> 16) & 0x0F;
    NVClk  = (N * chip->CrystalFreqKHz / M) >> P;
    cfg1 = NV_RD32(&chip->PFB[0x00000204/4], 0);
    sim_data.pix_bpp        = (char)pixelDepth;
    sim_data.enable_video   = 0;
    sim_data.enable_mp      = 0;
    sim_data.memory_type    = (NV_RD32(&chip->PFB[0x00000200/4], 0) & 0x01) ?
	1 : 0;
    sim_data.memory_width   = (NV_RD32(&chip->PEXTDEV[0x00000000/4], 0) & 0x10) ?
	128 : 64;
    sim_data.mem_latency    = (char)cfg1 & 0x0F;
    sim_data.mem_aligned    = 1;
    sim_data.mem_page_miss  = (char)(((cfg1 >> 4) &0x0F) + ((cfg1 >> 31) & 0x01));
    sim_data.gr_during_vid  = 0;
    sim_data.pclk_khz       = VClk;
    sim_data.mclk_khz       = MClk;
    sim_data.nvclk_khz      = NVClk;
    nv10CalcArbitration(&fifo_data, &sim_data);
    if (fifo_data.valid)
    {
        int  b = fifo_data.graphics_burst_size >> 4;
        *burst = 0;
        while (b >>= 1)
	    (*burst)++;
        *lwm   = fifo_data.graphics_lwm >> 3;
    }
}

static void nForceUpdateArbitrationSettings
(
    unsigned      VClk,
    unsigned      pixelDepth,
    unsigned     *burst,
    unsigned     *lwm,
    RIVA_HW_INST *chip,
    struct pci_dev *pdev
)
{
    nv10_fifo_info fifo_data;
    nv10_sim_state sim_data;
    unsigned int M, N, P, pll, MClk, NVClk;
    unsigned int uMClkPostDiv;
    struct pci_dev *dev;
    int domain = pci_domain_nr(pdev->bus);

    dev = pci_get_domain_bus_and_slot(domain, 0, 3);
    pci_read_config_dword(dev, 0x6C, &uMClkPostDiv);
    pci_dev_put(dev);
    uMClkPostDiv = (uMClkPostDiv >> 8) & 0xf;

    if(!uMClkPostDiv) uMClkPostDiv = 4;
    MClk = 400000 / uMClkPostDiv;

    pll = NV_RD32(&chip->PRAMDAC0[0x00000500/4], 0);
    M = (pll >> 0)  & 0xFF; N = (pll >> 8)  & 0xFF; P = (pll >> 16) & 0x0F;
    NVClk  = (N * chip->CrystalFreqKHz / M) >> P;
    sim_data.pix_bpp        = (char)pixelDepth;
    sim_data.enable_video   = 0;
    sim_data.enable_mp      = 0;

    dev = pci_get_domain_bus_and_slot(domain, 0, 1);
    pci_read_config_dword(dev, 0x7C, &sim_data.memory_type);
    pci_dev_put(dev);
    sim_data.memory_type    = (sim_data.memory_type >> 12) & 1;

    sim_data.memory_width   = 64;
    sim_data.mem_latency    = 3;
    sim_data.mem_aligned    = 1;
    sim_data.mem_page_miss  = 10;
    sim_data.gr_during_vid  = 0;
    sim_data.pclk_khz       = VClk;
    sim_data.mclk_khz       = MClk;
    sim_data.nvclk_khz      = NVClk;
    nv10CalcArbitration(&fifo_data, &sim_data);
    if (fifo_data.valid)
    {
        int  b = fifo_data.graphics_burst_size >> 4;
        *burst = 0;
        while (b >>= 1)
	    (*burst)++;
        *lwm   = fifo_data.graphics_lwm >> 3;
    }
}

/****************************************************************************\
*                                                                            *
*                          RIVA Mode State Routines                          *
*                                                                            *
\****************************************************************************/

/*
 * Calculate the Video Clock parameters for the PLL.
 */
static int CalcVClock
(
    int           clockIn,
    int          *clockOut,
    int          *mOut,
    int          *nOut,
    int          *pOut,
    RIVA_HW_INST *chip
)
{
    unsigned lowM, highM, highP;
    unsigned DeltaNew, DeltaOld;
    unsigned VClk, Freq;
    unsigned M, N, P;
    
    DeltaOld = 0xFFFFFFFF;

    VClk     = (unsigned)clockIn;
    
    if (chip->CrystalFreqKHz == 13500)
    {
        lowM  = 7;
        highM = 13 - (chip->Architecture == NV_ARCH_03);
    }
    else
    {
        lowM  = 8;
        highM = 14 - (chip->Architecture == NV_ARCH_03);
    }                      

    highP = 4 - (chip->Architecture == NV_ARCH_03);
    for (P = 0; P <= highP; P ++)
    {
        Freq = VClk << P;
        if ((Freq >= 128000) && (Freq <= chip->MaxVClockFreqKHz))
        {
            for (M = lowM; M <= highM; M++)
            {
                N    = (VClk << P) * M / chip->CrystalFreqKHz;
                if(N <= 255) {
                Freq = (chip->CrystalFreqKHz * N / M) >> P;
                if (Freq > VClk)
                    DeltaNew = Freq - VClk;
                else
                    DeltaNew = VClk - Freq;
                if (DeltaNew < DeltaOld)
                {
                    *mOut     = M;
                    *nOut     = N;
                    *pOut     = P;
                    *clockOut = Freq;
                    DeltaOld  = DeltaNew;
                }
            }
        }
    }
    }

    /* non-zero: M/N/P/clock values assigned.  zero: error (not set) */
    return (DeltaOld != 0xFFFFFFFF);
}
/*
 * Calculate extended mode parameters (SVGA) and save in a 
 * mode state structure.
 */
int CalcStateExt
(
    RIVA_HW_INST  *chip,
    RIVA_HW_STATE *state,
    struct pci_dev *pdev,
    int            bpp,
    int            width,
    int            hDisplaySize,
    int            height,
    int            dotClock
)
{
    int pixelDepth;
    int VClk, m, n, p;

    /*
     * Save mode parameters.
     */
    state->bpp    = bpp;    /* this is not bitsPerPixel, it's 8,15,16,32 */
    state->width  = width;
    state->height = height;
    /*
     * Extended RIVA registers.
     */
    pixelDepth = (bpp + 1)/8;
    if (!CalcVClock(dotClock, &VClk, &m, &n, &p, chip))
    	return -EINVAL;

    switch (chip->Architecture)
    {
        case NV_ARCH_03:
            nv3UpdateArbitrationSettings(VClk, 
                                         pixelDepth * 8, 
                                        &(state->arbitration0),
                                        &(state->arbitration1),
                                         chip);
            state->cursor0  = 0x00;
            state->cursor1  = 0x78;
            state->cursor2  = 0x00000000;
            state->pllsel   = 0x10010100;
            state->config   = ((width + 31)/32)
                            | (((pixelDepth > 2) ? 3 : pixelDepth) << 8)
                            | 0x1000;
            state->general  = 0x00100100;
            state->repaint1 = hDisplaySize < 1280 ? 0x06 : 0x02;
            break;
        case NV_ARCH_04:
            nv4UpdateArbitrationSettings(VClk, 
                                         pixelDepth * 8, 
                                        &(state->arbitration0),
                                        &(state->arbitration1),
                                         chip);
            state->cursor0  = 0x00;
            state->cursor1  = 0xFC;
            state->cursor2  = 0x00000000;
            state->pllsel   = 0x10000700;
            state->config   = 0x00001114;
            state->general  = bpp == 16 ? 0x00101100 : 0x00100100;
            state->repaint1 = hDisplaySize < 1280 ? 0x04 : 0x00;
            break;
        case NV_ARCH_10:
        case NV_ARCH_20:
        case NV_ARCH_30:
            if((chip->Chipset == NV_CHIP_IGEFORCE2) ||
               (chip->Chipset == NV_CHIP_0x01F0))
            {
                nForceUpdateArbitrationSettings(VClk,
                                          pixelDepth * 8,
                                         &(state->arbitration0),
                                         &(state->arbitration1),
                                          chip, pdev);
            } else {
                nv10UpdateArbitrationSettings(VClk, 
                                          pixelDepth * 8, 
                                         &(state->arbitration0),
                                         &(state->arbitration1),
                                          chip);
            }
            state->cursor0  = 0x80 | (chip->CursorStart >> 17);
            state->cursor1  = (chip->CursorStart >> 11) << 2;
            state->cursor2  = chip->CursorStart >> 24;
            state->pllsel   = 0x10000700;
            state->config   = NV_RD32(&chip->PFB[0x00000200/4], 0);
            state->general  = bpp == 16 ? 0x00101100 : 0x00100100;
            state->repaint1 = hDisplaySize < 1280 ? 0x04 : 0x00;
            break;
    }

     /* Paul Richards: below if block borks things in kernel for some reason */
     /* Tony: Below is needed to set hardware in DirectColor */
    if((bpp != 8) && (chip->Architecture != NV_ARCH_03))
	    state->general |= 0x00000030;

    state->vpll     = (p << 16) | (n << 8) | m;
    state->repaint0 = (((width/8)*pixelDepth) & 0x700) >> 3;
    state->pixel    = pixelDepth > 2   ? 3    : pixelDepth;
    state->offset0  =
    state->offset1  =
    state->offset2  =
    state->offset3  = 0;
    state->pitch0   =
    state->pitch1   =
    state->pitch2   =
    state->pitch3   = pixelDepth * width;

    return 0;
}
/*
 * Load fixed function state and pre-calculated/stored state.
 */
#define LOAD_FIXED_STATE(tbl,dev)                                       \
    for (i = 0; i < sizeof(tbl##Table##dev)/8; i++)                 \
        NV_WR32(&chip->dev[tbl##Table##dev[i][0]], 0, tbl##Table##dev[i][1])
#define LOAD_FIXED_STATE_8BPP(tbl,dev)                                  \
    for (i = 0; i < sizeof(tbl##Table##dev##_8BPP)/8; i++)            \
        NV_WR32(&chip->dev[tbl##Table##dev##_8BPP[i][0]], 0, tbl##Table##dev##_8BPP[i][1])
#define LOAD_FIXED_STATE_15BPP(tbl,dev)                                 \
    for (i = 0; i < sizeof(tbl##Table##dev##_15BPP)/8; i++)           \
        NV_WR32(&chip->dev[tbl##Table##dev##_15BPP[i][0]], 0, tbl##Table##dev##_15BPP[i][1])
#define LOAD_FIXED_STATE_16BPP(tbl,dev)                                 \
    for (i = 0; i < sizeof(tbl##Table##dev##_16BPP)/8; i++)           \
        NV_WR32(&chip->dev[tbl##Table##dev##_16BPP[i][0]], 0, tbl##Table##dev##_16BPP[i][1])
#define LOAD_FIXED_STATE_32BPP(tbl,dev)                                 \
    for (i = 0; i < sizeof(tbl##Table##dev##_32BPP)/8; i++)           \
        NV_WR32(&chip->dev[tbl##Table##dev##_32BPP[i][0]], 0, tbl##Table##dev##_32BPP[i][1])

static void UpdateFifoState
(
    RIVA_HW_INST  *chip
)
{
    int i;

    switch (chip->Architecture)
    {
        case NV_ARCH_04:
            LOAD_FIXED_STATE(nv4,FIFO);
            chip->Tri03 = NULL;
            chip->Tri05 = (RivaTexturedTriangle05 __iomem *)&(chip->FIFO[0x0000E000/4]);
            break;
        case NV_ARCH_10:
        case NV_ARCH_20:
        case NV_ARCH_30:
            /*
             * Initialize state for the RivaTriangle3D05 routines.
             */
            LOAD_FIXED_STATE(nv10tri05,PGRAPH);
            LOAD_FIXED_STATE(nv10,FIFO);
            chip->Tri03 = NULL;
            chip->Tri05 = (RivaTexturedTriangle05 __iomem *)&(chip->FIFO[0x0000E000/4]);
            break;
    }
}
static void LoadStateExt
(
    RIVA_HW_INST  *chip,
    RIVA_HW_STATE *state
)
{
    int i;

    /*
     * Load HW fixed function state.
     */
    LOAD_FIXED_STATE(Riva,PMC);
    LOAD_FIXED_STATE(Riva,PTIMER);
    switch (chip->Architecture)
    {
        case NV_ARCH_03:
            /*
             * Make sure frame buffer config gets set before loading PRAMIN.
             */
            NV_WR32(chip->PFB, 0x00000200, state->config);
            LOAD_FIXED_STATE(nv3,PFIFO);
            LOAD_FIXED_STATE(nv3,PRAMIN);
            LOAD_FIXED_STATE(nv3,PGRAPH);
            switch (state->bpp)
            {
                case 15:
                case 16:
                    LOAD_FIXED_STATE_15BPP(nv3,PRAMIN);
                    LOAD_FIXED_STATE_15BPP(nv3,PGRAPH);
                    chip->Tri03 = (RivaTexturedTriangle03  __iomem *)&(chip->FIFO[0x0000E000/4]);
                    break;
                case 24:
                case 32:
                    LOAD_FIXED_STATE_32BPP(nv3,PRAMIN);
                    LOAD_FIXED_STATE_32BPP(nv3,PGRAPH);
                    chip->Tri03 = NULL;
                    break;
                case 8:
                default:
                    LOAD_FIXED_STATE_8BPP(nv3,PRAMIN);
                    LOAD_FIXED_STATE_8BPP(nv3,PGRAPH);
                    chip->Tri03 = NULL;
                    break;
            }
            for (i = 0x00000; i < 0x00800; i++)
                NV_WR32(&chip->PRAMIN[0x00000502 + i], 0, (i << 12) | 0x03);
            NV_WR32(chip->PGRAPH, 0x00000630, state->offset0);
            NV_WR32(chip->PGRAPH, 0x00000634, state->offset1);
            NV_WR32(chip->PGRAPH, 0x00000638, state->offset2);
            NV_WR32(chip->PGRAPH, 0x0000063C, state->offset3);
            NV_WR32(chip->PGRAPH, 0x00000650, state->pitch0);
            NV_WR32(chip->PGRAPH, 0x00000654, state->pitch1);
            NV_WR32(chip->PGRAPH, 0x00000658, state->pitch2);
            NV_WR32(chip->PGRAPH, 0x0000065C, state->pitch3);
            break;
        case NV_ARCH_04:
            /*
             * Make sure frame buffer config gets set before loading PRAMIN.
             */
            NV_WR32(chip->PFB, 0x00000200, state->config);
            LOAD_FIXED_STATE(nv4,PFIFO);
            LOAD_FIXED_STATE(nv4,PRAMIN);
            LOAD_FIXED_STATE(nv4,PGRAPH);
            switch (state->bpp)
            {
                case 15:
                    LOAD_FIXED_STATE_15BPP(nv4,PRAMIN);
                    LOAD_FIXED_STATE_15BPP(nv4,PGRAPH);
                    chip->Tri03 = (RivaTexturedTriangle03  __iomem *)&(chip->FIFO[0x0000E000/4]);
                    break;
                case 16:
                    LOAD_FIXED_STATE_16BPP(nv4,PRAMIN);
                    LOAD_FIXED_STATE_16BPP(nv4,PGRAPH);
                    chip->Tri03 = (RivaTexturedTriangle03  __iomem *)&(chip->FIFO[0x0000E000/4]);
                    break;
                case 24:
                case 32:
                    LOAD_FIXED_STATE_32BPP(nv4,PRAMIN);
                    LOAD_FIXED_STATE_32BPP(nv4,PGRAPH);
                    chip->Tri03 = NULL;
                    break;
                case 8:
                default:
                    LOAD_FIXED_STATE_8BPP(nv4,PRAMIN);
                    LOAD_FIXED_STATE_8BPP(nv4,PGRAPH);
                    chip->Tri03 = NULL;
                    break;
            }
            NV_WR32(chip->PGRAPH, 0x00000640, state->offset0);
            NV_WR32(chip->PGRAPH, 0x00000644, state->offset1);
            NV_WR32(chip->PGRAPH, 0x00000648, state->offset2);
            NV_WR32(chip->PGRAPH, 0x0000064C, state->offset3);
            NV_WR32(chip->PGRAPH, 0x00000670, state->pitch0);
            NV_WR32(chip->PGRAPH, 0x00000674, state->pitch1);
            NV_WR32(chip->PGRAPH, 0x00000678, state->pitch2);
            NV_WR32(chip->PGRAPH, 0x0000067C, state->pitch3);
            break;
        case NV_ARCH_10:
        case NV_ARCH_20:
        case NV_ARCH_30:
            if(chip->twoHeads) {
               VGA_WR08(chip->PCIO, 0x03D4, 0x44);
               VGA_WR08(chip->PCIO, 0x03D5, state->crtcOwner);
               chip->LockUnlock(chip, 0);
            }

            LOAD_FIXED_STATE(nv10,PFIFO);
            LOAD_FIXED_STATE(nv10,PRAMIN);
            LOAD_FIXED_STATE(nv10,PGRAPH);
            switch (state->bpp)
            {
                case 15:
                    LOAD_FIXED_STATE_15BPP(nv10,PRAMIN);
                    LOAD_FIXED_STATE_15BPP(nv10,PGRAPH);
                    chip->Tri03 = (RivaTexturedTriangle03  __iomem *)&(chip->FIFO[0x0000E000/4]);
                    break;
                case 16:
                    LOAD_FIXED_STATE_16BPP(nv10,PRAMIN);
                    LOAD_FIXED_STATE_16BPP(nv10,PGRAPH);
                    chip->Tri03 = (RivaTexturedTriangle03  __iomem *)&(chip->FIFO[0x0000E000/4]);
                    break;
                case 24:
                case 32:
                    LOAD_FIXED_STATE_32BPP(nv10,PRAMIN);
                    LOAD_FIXED_STATE_32BPP(nv10,PGRAPH);
                    chip->Tri03 = NULL;
                    break;
                case 8:
                default:
                    LOAD_FIXED_STATE_8BPP(nv10,PRAMIN);
                    LOAD_FIXED_STATE_8BPP(nv10,PGRAPH);
                    chip->Tri03 = NULL;
                    break;
            }

            if(chip->Architecture == NV_ARCH_10) {
                NV_WR32(chip->PGRAPH, 0x00000640, state->offset0);
                NV_WR32(chip->PGRAPH, 0x00000644, state->offset1);
                NV_WR32(chip->PGRAPH, 0x00000648, state->offset2);
                NV_WR32(chip->PGRAPH, 0x0000064C, state->offset3);
                NV_WR32(chip->PGRAPH, 0x00000670, state->pitch0);
                NV_WR32(chip->PGRAPH, 0x00000674, state->pitch1);
                NV_WR32(chip->PGRAPH, 0x00000678, state->pitch2);
                NV_WR32(chip->PGRAPH, 0x0000067C, state->pitch3);
                NV_WR32(chip->PGRAPH, 0x00000680, state->pitch3);
        } else {
        NV_WR32(chip->PGRAPH, 0x00000820, state->offset0);
        NV_WR32(chip->PGRAPH, 0x00000824, state->offset1);
        NV_WR32(chip->PGRAPH, 0x00000828, state->offset2);
        NV_WR32(chip->PGRAPH, 0x0000082C, state->offset3);
        NV_WR32(chip->PGRAPH, 0x00000850, state->pitch0);
        NV_WR32(chip->PGRAPH, 0x00000854, state->pitch1);
        NV_WR32(chip->PGRAPH, 0x00000858, state->pitch2);
        NV_WR32(chip->PGRAPH, 0x0000085C, state->pitch3);
        NV_WR32(chip->PGRAPH, 0x00000860, state->pitch3);
        NV_WR32(chip->PGRAPH, 0x00000864, state->pitch3);
        NV_WR32(chip->PGRAPH, 0x000009A4, NV_RD32(chip->PFB, 0x00000200));
        NV_WR32(chip->PGRAPH, 0x000009A8, NV_RD32(chip->PFB, 0x00000204));
        }
            if(chip->twoHeads) {
               NV_WR32(chip->PCRTC0, 0x00000860, state->head);
               NV_WR32(chip->PCRTC0, 0x00002860, state->head2);
            }
            NV_WR32(chip->PRAMDAC, 0x00000404, NV_RD32(chip->PRAMDAC, 0x00000404) | (1 << 25));

            NV_WR32(chip->PMC, 0x00008704, 1);
            NV_WR32(chip->PMC, 0x00008140, 0);
            NV_WR32(chip->PMC, 0x00008920, 0);
            NV_WR32(chip->PMC, 0x00008924, 0);
            NV_WR32(chip->PMC, 0x00008908, 0x01ffffff);
            NV_WR32(chip->PMC, 0x0000890C, 0x01ffffff);
            NV_WR32(chip->PMC, 0x00001588, 0);

            NV_WR32(chip->PFB, 0x00000240, 0);
            NV_WR32(chip->PFB, 0x00000250, 0);
            NV_WR32(chip->PFB, 0x00000260, 0);
            NV_WR32(chip->PFB, 0x00000270, 0);
            NV_WR32(chip->PFB, 0x00000280, 0);
            NV_WR32(chip->PFB, 0x00000290, 0);
            NV_WR32(chip->PFB, 0x000002A0, 0);
            NV_WR32(chip->PFB, 0x000002B0, 0);

            NV_WR32(chip->PGRAPH, 0x00000B00, NV_RD32(chip->PFB, 0x00000240));
            NV_WR32(chip->PGRAPH, 0x00000B04, NV_RD32(chip->PFB, 0x00000244));
            NV_WR32(chip->PGRAPH, 0x00000B08, NV_RD32(chip->PFB, 0x00000248));
            NV_WR32(chip->PGRAPH, 0x00000B0C, NV_RD32(chip->PFB, 0x0000024C));
            NV_WR32(chip->PGRAPH, 0x00000B10, NV_RD32(chip->PFB, 0x00000250));
            NV_WR32(chip->PGRAPH, 0x00000B14, NV_RD32(chip->PFB, 0x00000254));
            NV_WR32(chip->PGRAPH, 0x00000B18, NV_RD32(chip->PFB, 0x00000258));
            NV_WR32(chip->PGRAPH, 0x00000B1C, NV_RD32(chip->PFB, 0x0000025C));
            NV_WR32(chip->PGRAPH, 0x00000B20, NV_RD32(chip->PFB, 0x00000260));
            NV_WR32(chip->PGRAPH, 0x00000B24, NV_RD32(chip->PFB, 0x00000264));
            NV_WR32(chip->PGRAPH, 0x00000B28, NV_RD32(chip->PFB, 0x00000268));
            NV_WR32(chip->PGRAPH, 0x00000B2C, NV_RD32(chip->PFB, 0x0000026C));
            NV_WR32(chip->PGRAPH, 0x00000B30, NV_RD32(chip->PFB, 0x00000270));
            NV_WR32(chip->PGRAPH, 0x00000B34, NV_RD32(chip->PFB, 0x00000274));
            NV_WR32(chip->PGRAPH, 0x00000B38, NV_RD32(chip->PFB, 0x00000278));
            NV_WR32(chip->PGRAPH, 0x00000B3C, NV_RD32(chip->PFB, 0x0000027C));
            NV_WR32(chip->PGRAPH, 0x00000B40, NV_RD32(chip->PFB, 0x00000280));
            NV_WR32(chip->PGRAPH, 0x00000B44, NV_RD32(chip->PFB, 0x00000284));
            NV_WR32(chip->PGRAPH, 0x00000B48, NV_RD32(chip->PFB, 0x00000288));
            NV_WR32(chip->PGRAPH, 0x00000B4C, NV_RD32(chip->PFB, 0x0000028C));
            NV_WR32(chip->PGRAPH, 0x00000B50, NV_RD32(chip->PFB, 0x00000290));
            NV_WR32(chip->PGRAPH, 0x00000B54, NV_RD32(chip->PFB, 0x00000294));
            NV_WR32(chip->PGRAPH, 0x00000B58, NV_RD32(chip->PFB, 0x00000298));
            NV_WR32(chip->PGRAPH, 0x00000B5C, NV_RD32(chip->PFB, 0x0000029C));
            NV_WR32(chip->PGRAPH, 0x00000B60, NV_RD32(chip->PFB, 0x000002A0));
            NV_WR32(chip->PGRAPH, 0x00000B64, NV_RD32(chip->PFB, 0x000002A4));
            NV_WR32(chip->PGRAPH, 0x00000B68, NV_RD32(chip->PFB, 0x000002A8));
            NV_WR32(chip->PGRAPH, 0x00000B6C, NV_RD32(chip->PFB, 0x000002AC));
            NV_WR32(chip->PGRAPH, 0x00000B70, NV_RD32(chip->PFB, 0x000002B0));
            NV_WR32(chip->PGRAPH, 0x00000B74, NV_RD32(chip->PFB, 0x000002B4));
            NV_WR32(chip->PGRAPH, 0x00000B78, NV_RD32(chip->PFB, 0x000002B8));
            NV_WR32(chip->PGRAPH, 0x00000B7C, NV_RD32(chip->PFB, 0x000002BC));
            NV_WR32(chip->PGRAPH, 0x00000F40, 0x10000000);
            NV_WR32(chip->PGRAPH, 0x00000F44, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00000040);
            NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000008);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00000200);
            for (i = 0; i < (3*16); i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00000040);
            NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00000800);
            for (i = 0; i < (16*16); i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F40, 0x30000000);
            NV_WR32(chip->PGRAPH, 0x00000F44, 0x00000004);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00006400);
            for (i = 0; i < (59*4); i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00006800);
            for (i = 0; i < (47*4); i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00006C00);
            for (i = 0; i < (3*4); i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00007000);
            for (i = 0; i < (19*4); i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00007400);
            for (i = 0; i < (12*4); i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00007800);
            for (i = 0; i < (12*4); i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00004400);
            for (i = 0; i < (8*4); i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00000000);
            for (i = 0; i < 16; i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);
            NV_WR32(chip->PGRAPH, 0x00000F50, 0x00000040);
            for (i = 0; i < 4; i++)
                NV_WR32(chip->PGRAPH, 0x00000F54, 0x00000000);

            NV_WR32(chip->PCRTC, 0x00000810, state->cursorConfig);

            if(chip->flatPanel) {
               if((chip->Chipset & 0x0ff0) == 0x0110) {
                   NV_WR32(chip->PRAMDAC, 0x0528, state->dither);
               } else 
               if((chip->Chipset & 0x0ff0) >= 0x0170) {
                   NV_WR32(chip->PRAMDAC, 0x083C, state->dither);
               }
            
               VGA_WR08(chip->PCIO, 0x03D4, 0x53);
               VGA_WR08(chip->PCIO, 0x03D5, 0);
               VGA_WR08(chip->PCIO, 0x03D4, 0x54);
               VGA_WR08(chip->PCIO, 0x03D5, 0);
               VGA_WR08(chip->PCIO, 0x03D4, 0x21);
               VGA_WR08(chip->PCIO, 0x03D5, 0xfa);
            }

            VGA_WR08(chip->PCIO, 0x03D4, 0x41);
            VGA_WR08(chip->PCIO, 0x03D5, state->extra);
    }
    LOAD_FIXED_STATE(Riva,FIFO);
    UpdateFifoState(chip);
    /*
     * Load HW mode state.
     */
    VGA_WR08(chip->PCIO, 0x03D4, 0x19);
    VGA_WR08(chip->PCIO, 0x03D5, state->repaint0);
    VGA_WR08(chip->PCIO, 0x03D4, 0x1A);
    VGA_WR08(chip->PCIO, 0x03D5, state->repaint1);
    VGA_WR08(chip->PCIO, 0x03D4, 0x25);
    VGA_WR08(chip->PCIO, 0x03D5, state->screen);
    VGA_WR08(chip->PCIO, 0x03D4, 0x28);
    VGA_WR08(chip->PCIO, 0x03D5, state->pixel);
    VGA_WR08(chip->PCIO, 0x03D4, 0x2D);
    VGA_WR08(chip->PCIO, 0x03D5, state->horiz);
    VGA_WR08(chip->PCIO, 0x03D4, 0x1B);
    VGA_WR08(chip->PCIO, 0x03D5, state->arbitration0);
    VGA_WR08(chip->PCIO, 0x03D4, 0x20);
    VGA_WR08(chip->PCIO, 0x03D5, state->arbitration1);
    VGA_WR08(chip->PCIO, 0x03D4, 0x30);
    VGA_WR08(chip->PCIO, 0x03D5, state->cursor0);
    VGA_WR08(chip->PCIO, 0x03D4, 0x31);
    VGA_WR08(chip->PCIO, 0x03D5, state->cursor1);
    VGA_WR08(chip->PCIO, 0x03D4, 0x2F);
    VGA_WR08(chip->PCIO, 0x03D5, state->cursor2);
    VGA_WR08(chip->PCIO, 0x03D4, 0x39);
    VGA_WR08(chip->PCIO, 0x03D5, state->interlace);

    if(!chip->flatPanel) {
       NV_WR32(chip->PRAMDAC0, 0x00000508, state->vpll);
       NV_WR32(chip->PRAMDAC0, 0x0000050C, state->pllsel);
       if(chip->twoHeads)
          NV_WR32(chip->PRAMDAC0, 0x00000520, state->vpll2);
    }  else {
       NV_WR32(chip->PRAMDAC, 0x00000848 , state->scale);
    }  
    NV_WR32(chip->PRAMDAC, 0x00000600 , state->general);

    /*
     * Turn off VBlank enable and reset.
     */
    NV_WR32(chip->PCRTC, 0x00000140, 0);
    NV_WR32(chip->PCRTC, 0x00000100, chip->VBlankBit);
    /*
     * Set interrupt enable.
     */    
    NV_WR32(chip->PMC, 0x00000140, chip->EnableIRQ & 0x01);
    /*
     * Set current state pointer.
     */
    chip->CurrentState = state;
    /*
     * Reset FIFO free and empty counts.
     */
    chip->FifoFreeCount  = 0;
    /* Free count from first subchannel */
    chip->FifoEmptyCount = NV_RD32(&chip->Rop->FifoFree, 0);
}
static void UnloadStateExt
(
    RIVA_HW_INST  *chip,
    RIVA_HW_STATE *state
)
{
    /*
     * Save current HW state.
     */
    VGA_WR08(chip->PCIO, 0x03D4, 0x19);
    state->repaint0     = VGA_RD08(chip->PCIO, 0x03D5);
    VGA_WR08(chip->PCIO, 0x03D4, 0x1A);
    state->repaint1     = VGA_RD08(chip->PCIO, 0x03D5);
    VGA_WR08(chip->PCIO, 0x03D4, 0x25);
    state->screen       = VGA_RD08(chip->PCIO, 0x03D5);
    VGA_WR08(chip->PCIO, 0x03D4, 0x28);
    state->pixel        = VGA_RD08(chip->PCIO, 0x03D5);
    VGA_WR08(chip->PCIO, 0x03D4, 0x2D);
    state->horiz        = VGA_RD08(chip->PCIO, 0x03D5);
    VGA_WR08(chip->PCIO, 0x03D4, 0x1B);
    state->arbitration0 = VGA_RD08(chip->PCIO, 0x03D5);
    VGA_WR08(chip->PCIO, 0x03D4, 0x20);
    state->arbitration1 = VGA_RD08(chip->PCIO, 0x03D5);
    VGA_WR08(chip->PCIO, 0x03D4, 0x30);
    state->cursor0      = VGA_RD08(chip->PCIO, 0x03D5);
    VGA_WR08(chip->PCIO, 0x03D4, 0x31);
    state->cursor1      = VGA_RD08(chip->PCIO, 0x03D5);
    VGA_WR08(chip->PCIO, 0x03D4, 0x2F);
    state->cursor2      = VGA_RD08(chip->PCIO, 0x03D5);
    VGA_WR08(chip->PCIO, 0x03D4, 0x39);
    state->interlace    = VGA_RD08(chip->PCIO, 0x03D5);
    state->vpll         = NV_RD32(chip->PRAMDAC0, 0x00000508);
    state->vpll2        = NV_RD32(chip->PRAMDAC0, 0x00000520);
    state->pllsel       = NV_RD32(chip->PRAMDAC0, 0x0000050C);
    state->general      = NV_RD32(chip->PRAMDAC, 0x00000600);
    state->scale        = NV_RD32(chip->PRAMDAC, 0x00000848);
    state->config       = NV_RD32(chip->PFB, 0x00000200);
    switch (chip->Architecture)
    {
        case NV_ARCH_03:
            state->offset0  = NV_RD32(chip->PGRAPH, 0x00000630);
            state->offset1  = NV_RD32(chip->PGRAPH, 0x00000634);
            state->offset2  = NV_RD32(chip->PGRAPH, 0x00000638);
            state->offset3  = NV_RD32(chip->PGRAPH, 0x0000063C);
            state->pitch0   = NV_RD32(chip->PGRAPH, 0x00000650);
            state->pitch1   = NV_RD32(chip->PGRAPH, 0x00000654);
            state->pitch2   = NV_RD32(chip->PGRAPH, 0x00000658);
            state->pitch3   = NV_RD32(chip->PGRAPH, 0x0000065C);
            break;
        case NV_ARCH_04:
            state->offset0  = NV_RD32(chip->PGRAPH, 0x00000640);
            state->offset1  = NV_RD32(chip->PGRAPH, 0x00000644);
            state->offset2  = NV_RD32(chip->PGRAPH, 0x00000648);
            state->offset3  = NV_RD32(chip->PGRAPH, 0x0000064C);
            state->pitch0   = NV_RD32(chip->PGRAPH, 0x00000670);
            state->pitch1   = NV_RD32(chip->PGRAPH, 0x00000674);
            state->pitch2   = NV_RD32(chip->PGRAPH, 0x00000678);
            state->pitch3   = NV_RD32(chip->PGRAPH, 0x0000067C);
            break;
        case NV_ARCH_10:
        case NV_ARCH_20:
        case NV_ARCH_30:
            state->offset0  = NV_RD32(chip->PGRAPH, 0x00000640);
            state->offset1  = NV_RD32(chip->PGRAPH, 0x00000644);
            state->offset2  = NV_RD32(chip->PGRAPH, 0x00000648);
            state->offset3  = NV_RD32(chip->PGRAPH, 0x0000064C);
            state->pitch0   = NV_RD32(chip->PGRAPH, 0x00000670);
            state->pitch1   = NV_RD32(chip->PGRAPH, 0x00000674);
            state->pitch2   = NV_RD32(chip->PGRAPH, 0x00000678);
            state->pitch3   = NV_RD32(chip->PGRAPH, 0x0000067C);
            if(chip->twoHeads) {
               state->head     = NV_RD32(chip->PCRTC0, 0x00000860);
               state->head2    = NV_RD32(chip->PCRTC0, 0x00002860);
               VGA_WR08(chip->PCIO, 0x03D4, 0x44);
               state->crtcOwner = VGA_RD08(chip->PCIO, 0x03D5);
            }
            VGA_WR08(chip->PCIO, 0x03D4, 0x41);
            state->extra = VGA_RD08(chip->PCIO, 0x03D5);
            state->cursorConfig = NV_RD32(chip->PCRTC, 0x00000810);

            if((chip->Chipset & 0x0ff0) == 0x0110) {
                state->dither = NV_RD32(chip->PRAMDAC, 0x0528);
            } else 
            if((chip->Chipset & 0x0ff0) >= 0x0170) {
                state->dither = NV_RD32(chip->PRAMDAC, 0x083C);
            }
            break;
    }
}
static void SetStartAddress
(
    RIVA_HW_INST *chip,
    unsigned      start
)
{
    NV_WR32(chip->PCRTC, 0x800, start);
}

static void SetStartAddress3
(
    RIVA_HW_INST *chip,
    unsigned      start
)
{
    int offset = start >> 2;
    int pan    = (start & 3) << 1;
    unsigned char tmp;

    /*
     * Unlock extended registers.
     */
    chip->LockUnlock(chip, 0);
    /*
     * Set start address.
     */
    VGA_WR08(chip->PCIO, 0x3D4, 0x0D); VGA_WR08(chip->PCIO, 0x3D5, offset);
    offset >>= 8;
    VGA_WR08(chip->PCIO, 0x3D4, 0x0C); VGA_WR08(chip->PCIO, 0x3D5, offset);
    offset >>= 8;
    VGA_WR08(chip->PCIO, 0x3D4, 0x19); tmp = VGA_RD08(chip->PCIO, 0x3D5);
    VGA_WR08(chip->PCIO, 0x3D5, (offset & 0x01F) | (tmp & ~0x1F));
    VGA_WR08(chip->PCIO, 0x3D4, 0x2D); tmp = VGA_RD08(chip->PCIO, 0x3D5);
    VGA_WR08(chip->PCIO, 0x3D5, (offset & 0x60) | (tmp & ~0x60));
    /*
     * 4 pixel pan register.
     */
    offset = VGA_RD08(chip->PCIO, chip->IO + 0x0A);
    VGA_WR08(chip->PCIO, 0x3C0, 0x13);
    VGA_WR08(chip->PCIO, 0x3C0, pan);
}
static void nv3SetSurfaces2D
(
    RIVA_HW_INST *chip,
    unsigned     surf0,
    unsigned     surf1
)
{
    RivaSurface __iomem *Surface =
	(RivaSurface __iomem *)&(chip->FIFO[0x0000E000/4]);

    RIVA_FIFO_FREE(*chip,Tri03,5);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000003);
    NV_WR32(&Surface->Offset, 0, surf0);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000004);
    NV_WR32(&Surface->Offset, 0, surf1);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000013);
}
static void nv4SetSurfaces2D
(
    RIVA_HW_INST *chip,
    unsigned     surf0,
    unsigned     surf1
)
{
    RivaSurface __iomem *Surface =
	(RivaSurface __iomem *)&(chip->FIFO[0x0000E000/4]);

    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000003);
    NV_WR32(&Surface->Offset, 0, surf0);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000004);
    NV_WR32(&Surface->Offset, 0, surf1);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000014);
}
static void nv10SetSurfaces2D
(
    RIVA_HW_INST *chip,
    unsigned     surf0,
    unsigned     surf1
)
{
    RivaSurface __iomem *Surface =
	(RivaSurface __iomem *)&(chip->FIFO[0x0000E000/4]);

    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000003);
    NV_WR32(&Surface->Offset, 0, surf0);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000004);
    NV_WR32(&Surface->Offset, 0, surf1);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000014);
}
static void nv3SetSurfaces3D
(
    RIVA_HW_INST *chip,
    unsigned     surf0,
    unsigned     surf1
)
{
    RivaSurface __iomem *Surface =
	(RivaSurface __iomem *)&(chip->FIFO[0x0000E000/4]);

    RIVA_FIFO_FREE(*chip,Tri03,5);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000005);
    NV_WR32(&Surface->Offset, 0, surf0);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000006);
    NV_WR32(&Surface->Offset, 0, surf1);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000013);
}
static void nv4SetSurfaces3D
(
    RIVA_HW_INST *chip,
    unsigned     surf0,
    unsigned     surf1
)
{
    RivaSurface __iomem *Surface =
	(RivaSurface __iomem *)&(chip->FIFO[0x0000E000/4]);

    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000005);
    NV_WR32(&Surface->Offset, 0, surf0);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000006);
    NV_WR32(&Surface->Offset, 0, surf1);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000014);
}
static void nv10SetSurfaces3D
(
    RIVA_HW_INST *chip,
    unsigned     surf0,
    unsigned     surf1
)
{
    RivaSurface3D __iomem *Surfaces3D =
	(RivaSurface3D __iomem *)&(chip->FIFO[0x0000E000/4]);

    RIVA_FIFO_FREE(*chip,Tri03,4);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000007);
    NV_WR32(&Surfaces3D->RenderBufferOffset, 0, surf0);
    NV_WR32(&Surfaces3D->ZBufferOffset, 0, surf1);
    NV_WR32(&chip->FIFO[0x00003800], 0, 0x80000014);
}

/****************************************************************************\
*                                                                            *
*                      Probe RIVA Chip Configuration                         *
*                                                                            *
\****************************************************************************/

static void nv3GetConfig
(
    RIVA_HW_INST *chip
)
{
    /*
     * Fill in chip configuration.
     */
    if (NV_RD32(&chip->PFB[0x00000000/4], 0) & 0x00000020)
    {
        if (((NV_RD32(chip->PMC, 0x00000000) & 0xF0) == 0x20)
         && ((NV_RD32(chip->PMC, 0x00000000) & 0x0F) >= 0x02))
        {        
            /*
             * SDRAM 128 ZX.
             */
            chip->RamBandwidthKBytesPerSec = 800000;
            switch (NV_RD32(chip->PFB, 0x00000000) & 0x03)
            {
                case 2:
                    chip->RamAmountKBytes = 1024 * 4;
                    break;
                case 1:
                    chip->RamAmountKBytes = 1024 * 2;
                    break;
                default:
                    chip->RamAmountKBytes = 1024 * 8;
                    break;
            }
        }            
        else            
        {
            chip->RamBandwidthKBytesPerSec = 1000000;
            chip->RamAmountKBytes          = 1024 * 8;
        }            
    }
    else
    {
        /*
         * SGRAM 128.
         */
        chip->RamBandwidthKBytesPerSec = 1000000;
        switch (NV_RD32(chip->PFB, 0x00000000) & 0x00000003)
        {
            case 0:
                chip->RamAmountKBytes = 1024 * 8;
                break;
            case 2:
                chip->RamAmountKBytes = 1024 * 4;
                break;
            default:
                chip->RamAmountKBytes = 1024 * 2;
                break;
        }
    }        
    chip->CrystalFreqKHz   = (NV_RD32(chip->PEXTDEV, 0x00000000) & 0x00000040) ? 14318 : 13500;
    chip->CURSOR           = &(chip->PRAMIN[0x00008000/4 - 0x0800/4]);
    chip->VBlankBit        = 0x00000100;
    chip->MaxVClockFreqKHz = 256000;
    /*
     * Set chip functions.
     */
    chip->Busy            = nv3Busy;
    chip->ShowHideCursor  = ShowHideCursor;
    chip->LoadStateExt    = LoadStateExt;
    chip->UnloadStateExt  = UnloadStateExt;
    chip->SetStartAddress = SetStartAddress3;
    chip->SetSurfaces2D   = nv3SetSurfaces2D;
    chip->SetSurfaces3D   = nv3SetSurfaces3D;
    chip->LockUnlock      = nv3LockUnlock;
}
static void nv4GetConfig
(
    RIVA_HW_INST *chip
)
{
    /*
     * Fill in chip configuration.
     */
    if (NV_RD32(chip->PFB, 0x00000000) & 0x00000100)
    {
        chip->RamAmountKBytes = ((NV_RD32(chip->PFB, 0x00000000) >> 12) & 0x0F) * 1024 * 2
                              + 1024 * 2;
    }
    else
    {
        switch (NV_RD32(chip->PFB, 0x00000000) & 0x00000003)
        {
            case 0:
                chip->RamAmountKBytes = 1024 * 32;
                break;
            case 1:
                chip->RamAmountKBytes = 1024 * 4;
                break;
            case 2:
                chip->RamAmountKBytes = 1024 * 8;
                break;
            case 3:
            default:
                chip->RamAmountKBytes = 1024 * 16;
                break;
        }
    }
    switch ((NV_RD32(chip->PFB, 0x00000000) >> 3) & 0x00000003)
    {
        case 3:
            chip->RamBandwidthKBytesPerSec = 800000;
            break;
        default:
            chip->RamBandwidthKBytesPerSec = 1000000;
            break;
    }
    chip->CrystalFreqKHz   = (NV_RD32(chip->PEXTDEV, 0x00000000) & 0x00000040) ? 14318 : 13500;
    chip->CURSOR           = &(chip->PRAMIN[0x00010000/4 - 0x0800/4]);
    chip->VBlankBit        = 0x00000001;
    chip->MaxVClockFreqKHz = 350000;
    /*
     * Set chip functions.
     */
    chip->Busy            = nv4Busy;
    chip->ShowHideCursor  = ShowHideCursor;
    chip->LoadStateExt    = LoadStateExt;
    chip->UnloadStateExt  = UnloadStateExt;
    chip->SetStartAddress = SetStartAddress;
    chip->SetSurfaces2D   = nv4SetSurfaces2D;
    chip->SetSurfaces3D   = nv4SetSurfaces3D;
    chip->LockUnlock      = nv4LockUnlock;
}
static void nv10GetConfig
(
    RIVA_HW_INST *chip,
    struct pci_dev *pdev,
    unsigned int chipset
)
{
    struct pci_dev* dev;
    int domain = pci_domain_nr(pdev->bus);
    u32 amt;

#ifdef __BIG_ENDIAN
    /* turn on big endian register access */
    if(!(NV_RD32(chip->PMC, 0x00000004) & 0x01000001))
    	NV_WR32(chip->PMC, 0x00000004, 0x01000001);
#endif

    /*
     * Fill in chip configuration.
     */
    if(chipset == NV_CHIP_IGEFORCE2) {
        dev = pci_get_domain_bus_and_slot(domain, 0, 1);
        pci_read_config_dword(dev, 0x7C, &amt);
        pci_dev_put(dev);
        chip->RamAmountKBytes = (((amt >> 6) & 31) + 1) * 1024;
    } else if(chipset == NV_CHIP_0x01F0) {
        dev = pci_get_domain_bus_and_slot(domain, 0, 1);
        pci_read_config_dword(dev, 0x84, &amt);
        pci_dev_put(dev);
        chip->RamAmountKBytes = (((amt >> 4) & 127) + 1) * 1024;
    } else {
        switch ((NV_RD32(chip->PFB, 0x0000020C) >> 20) & 0x000000FF)
        {
            case 0x02:
                chip->RamAmountKBytes = 1024 * 2;
                break;
            case 0x04:
                chip->RamAmountKBytes = 1024 * 4;
                break;
            case 0x08:
                chip->RamAmountKBytes = 1024 * 8;
                break;
            case 0x10:
                chip->RamAmountKBytes = 1024 * 16;
                break;
            case 0x20:
                chip->RamAmountKBytes = 1024 * 32;
                break;
            case 0x40:
                chip->RamAmountKBytes = 1024 * 64;
                break;
            case 0x80:
                chip->RamAmountKBytes = 1024 * 128;
                break;
            default:
                chip->RamAmountKBytes = 1024 * 16;
                break;
        }
    }
    switch ((NV_RD32(chip->PFB, 0x00000000) >> 3) & 0x00000003)
    {
        case 3:
            chip->RamBandwidthKBytesPerSec = 800000;
            break;
        default:
            chip->RamBandwidthKBytesPerSec = 1000000;
            break;
    }
    chip->CrystalFreqKHz = (NV_RD32(chip->PEXTDEV, 0x0000) & (1 << 6)) ?
	14318 : 13500;

    switch (chipset & 0x0ff0) {
    case 0x0170:
    case 0x0180:
    case 0x01F0:
    case 0x0250:
    case 0x0280:
    case 0x0300:
    case 0x0310:
    case 0x0320:
    case 0x0330:
    case 0x0340:
       if(NV_RD32(chip->PEXTDEV, 0x0000) & (1 << 22))
           chip->CrystalFreqKHz = 27000;
       break;
    default:
       break;
    }

    chip->CursorStart      = (chip->RamAmountKBytes - 128) * 1024;
    chip->CURSOR           = NULL;  /* can't set this here */
    chip->VBlankBit        = 0x00000001;
    chip->MaxVClockFreqKHz = 350000;
    /*
     * Set chip functions.
     */
    chip->Busy            = nv10Busy;
    chip->ShowHideCursor  = ShowHideCursor;
    chip->LoadStateExt    = LoadStateExt;
    chip->UnloadStateExt  = UnloadStateExt;
    chip->SetStartAddress = SetStartAddress;
    chip->SetSurfaces2D   = nv10SetSurfaces2D;
    chip->SetSurfaces3D   = nv10SetSurfaces3D;
    chip->LockUnlock      = nv4LockUnlock;

    switch(chipset & 0x0ff0) {
    case 0x0110:
    case 0x0170:
    case 0x0180:
    case 0x01F0:
    case 0x0250:
    case 0x0280:
    case 0x0300:
    case 0x0310:
    case 0x0320:
    case 0x0330:
    case 0x0340:
        chip->twoHeads = TRUE;
        break;
    default:
        chip->twoHeads = FALSE;
        break;
    }
}
int RivaGetConfig
(
    RIVA_HW_INST *chip,
    struct pci_dev *pdev,
    unsigned int chipset
)
{
    /*
     * Save this so future SW know whats it's dealing with.
     */
    chip->Version = RIVA_SW_VERSION;
    /*
     * Chip specific configuration.
     */
    switch (chip->Architecture)
    {
        case NV_ARCH_03:
            nv3GetConfig(chip);
            break;
        case NV_ARCH_04:
            nv4GetConfig(chip);
            break;
        case NV_ARCH_10:
        case NV_ARCH_20:
        case NV_ARCH_30:
            nv10GetConfig(chip, pdev, chipset);
            break;
        default:
            return (-1);
    }
    chip->Chipset = chipset;
    /*
     * Fill in FIFO pointers.
     */
    chip->Rop    = (RivaRop __iomem         *)&(chip->FIFO[0x00000000/4]);
    chip->Clip   = (RivaClip __iomem        *)&(chip->FIFO[0x00002000/4]);
    chip->Patt   = (RivaPattern __iomem     *)&(chip->FIFO[0x00004000/4]);
    chip->Pixmap = (RivaPixmap __iomem      *)&(chip->FIFO[0x00006000/4]);
    chip->Blt    = (RivaScreenBlt __iomem   *)&(chip->FIFO[0x00008000/4]);
    chip->Bitmap = (RivaBitmap __iomem      *)&(chip->FIFO[0x0000A000/4]);
    chip->Line   = (RivaLine __iomem        *)&(chip->FIFO[0x0000C000/4]);
    chip->Tri03  = (RivaTexturedTriangle03 __iomem *)&(chip->FIFO[0x0000E000/4]);
    return (0);
}

