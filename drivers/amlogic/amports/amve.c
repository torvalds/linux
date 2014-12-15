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


#include <mach/am_regs.h>

#include "linux/amlogic/amports/vframe.h"
#include "linux/amlogic/amports/ve.h"

#include "ve_regs.h"
#include "amve.h"


static unsigned char ve_dnlp_tgt[64], ve_dnlp_rt;
static ulong ve_dnlp_lpf[64], ve_dnlp_reg[16];

static ulong ve_benh_inv[32][2] = { // [0]: inv_10_0, [1]: inv_11
    {2047, 1}, {2047, 1}, {   0, 1}, {1365, 0}, {1024, 0}, { 819, 0}, { 683, 0}, { 585, 0},
    { 512, 0}, { 455, 0}, { 410, 0}, { 372, 0}, { 341, 0}, { 315, 0}, { 293, 0}, { 273, 0},
    { 256, 0}, { 241, 0}, { 228, 0}, { 216, 0}, { 205, 0}, { 195, 0}, { 186, 0}, { 178, 0},
    { 171, 0}, { 164, 0}, { 158, 0}, { 152, 0}, { 146, 0}, { 141, 0}, { 137, 0}, { 132, 0},
};

static ulong ve_reg_limit(ulong val, ulong wid)
{
    if (val < (1 << wid)) {
        return(val);
    } else {
        return((1 << wid) - 1);
    }
}


// ***************************************************************************
// *** VPP_FIQ-oriented functions *********************************************
// ***************************************************************************

static void ve_dnlp_calculate_tgt(vframe_t *vf) // target (starting point) of a new seg is upon the total partition of the previous segs, so:
// tgt[0] is always 0 & no need calculation
// tgt[1] is calculated upon gamma[0]
// tgt[2] is calculated upon gamma[0~1]
// tgt[3] is calculated upon gamma[0~2]
// ...
// tgt[63] is calculated upon gamma[0~62], understood that gamma[63] will never be used
{
    struct vframe_prop_s *p = &vf->prop;
    ulong i = 0, flag = 0, sum = 0, tgt = 0, gain = 8 + 3 + ((p->hist.pixel_sum) >> 30), pixs = (p->hist.pixel_sum) & 0x3fffffff;

    for (i = 1; i < 64; i++) {
        if (!flag) {
            sum += p->hist.gamma[i - 1]; // sum of gamma[0] ~ gamma[i-1]
            tgt = (sum << gain) / pixs; // mapping to total 256 luminance
            if (tgt < 255) {
                ve_dnlp_tgt[i] = (unsigned char) tgt;
            } else {
                ve_dnlp_tgt[i] = 255;
                flag = 1;
            }
        } else {
            ve_dnlp_tgt[i] = 255;
        }
    }
}

static void ve_dnlp_calculate_lpf(void) // lpf[0] is always 0 & no need calculation
{
    ulong i = 0;

    for (i = 1; i < 64; i++) {
        ve_dnlp_lpf[i] = ve_dnlp_lpf[i] - (ve_dnlp_lpf[i] >> ve_dnlp_rt) + ve_dnlp_tgt[i];
    }
}

static void ve_dnlp_calculate_reg(void)
{
    ulong i = 0;

    for (i = 0; i < 16; i++) {
        ve_dnlp_reg[i]  =  ve_dnlp_lpf[ i << 2   ] >> ve_dnlp_rt     ;
        ve_dnlp_reg[i] |= (ve_dnlp_lpf[(i << 2) + 1] >> ve_dnlp_rt) << 8;
        ve_dnlp_reg[i] |= (ve_dnlp_lpf[(i << 2) + 2] >> ve_dnlp_rt) << 16;
        ve_dnlp_reg[i] |= (ve_dnlp_lpf[(i << 2) + 3] >> ve_dnlp_rt) << 24;
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

void ve_on_vs(vframe_t *vf)
{
    if (ve_dnlp_rt == VE_DNLP_RT_FREEZE) {
        return;
    }
    // calculate dnlp target data
    ve_dnlp_calculate_tgt(vf);
    // calculate dnlp low-pass-filter data
    ve_dnlp_calculate_lpf();
    // calculate dnlp reg data
    ve_dnlp_calculate_reg();
    // load dnlp reg data
    ve_dnlp_load_reg();
}


// ***************************************************************************
// *** IOCTL-oriented functions *********************************************
// ***************************************************************************

void ve_set_bext(struct ve_bext_s *p)
{
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, ve_reg_limit(p->en    , BEXT_EN_WID), BEXT_EN_BIT    , BEXT_EN_WID);
    WRITE_CBUS_REG_BITS(VPP_BLACKEXT_CTRL , ve_reg_limit(p->start , BEXT_START_WID), BEXT_START_BIT , BEXT_START_WID);
    WRITE_CBUS_REG_BITS(VPP_BLACKEXT_CTRL , ve_reg_limit(p->slope1, BEXT_SLOPE1_WID), BEXT_SLOPE1_BIT, BEXT_SLOPE1_WID);
    WRITE_CBUS_REG_BITS(VPP_BLACKEXT_CTRL , ve_reg_limit(p->midpt , BEXT_MIDPT_WID), BEXT_MIDPT_BIT , BEXT_MIDPT_WID);
    WRITE_CBUS_REG_BITS(VPP_BLACKEXT_CTRL , ve_reg_limit(p->slope2, BEXT_SLOPE2_WID), BEXT_SLOPE2_BIT, BEXT_SLOPE2_WID);
}



void ve_set_dnlp(struct ve_dnlp_s *p)
{
    ulong i = 0;

    ve_dnlp_rt = p->rt;
    if (!(p->en)) {
        ve_dnlp_rt = VE_DNLP_RT_FREEZE;
    }
    if (!(ve_dnlp_rt == VE_DNLP_RT_FREEZE)) {
        for (i = 0; i < 64; i++) {
            ve_dnlp_lpf[i] = (ulong)(p->gamma[i]) << (ulong)ve_dnlp_rt;
        }
    }
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, ve_reg_limit(p->en       , DNLP_EN_WID), DNLP_EN_BIT     , DNLP_EN_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_00  , ve_reg_limit(p->gamma[0],  DNLP_GAMMA00_WID), DNLP_GAMMA00_BIT, DNLP_GAMMA00_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_00  , ve_reg_limit(p->gamma[1],  DNLP_GAMMA01_WID), DNLP_GAMMA01_BIT, DNLP_GAMMA01_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_00  , ve_reg_limit(p->gamma[2],  DNLP_GAMMA02_WID), DNLP_GAMMA02_BIT, DNLP_GAMMA02_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_00  , ve_reg_limit(p->gamma[3],  DNLP_GAMMA03_WID), DNLP_GAMMA03_BIT, DNLP_GAMMA03_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_01  , ve_reg_limit(p->gamma[4],  DNLP_GAMMA04_WID), DNLP_GAMMA04_BIT, DNLP_GAMMA04_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_01  , ve_reg_limit(p->gamma[5],  DNLP_GAMMA05_WID), DNLP_GAMMA05_BIT, DNLP_GAMMA05_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_01  , ve_reg_limit(p->gamma[6],  DNLP_GAMMA06_WID), DNLP_GAMMA06_BIT, DNLP_GAMMA06_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_01  , ve_reg_limit(p->gamma[7],  DNLP_GAMMA07_WID), DNLP_GAMMA07_BIT, DNLP_GAMMA07_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_02  , ve_reg_limit(p->gamma[8],  DNLP_GAMMA08_WID), DNLP_GAMMA08_BIT, DNLP_GAMMA08_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_02  , ve_reg_limit(p->gamma[9],  DNLP_GAMMA09_WID), DNLP_GAMMA09_BIT, DNLP_GAMMA09_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_02  , ve_reg_limit(p->gamma[10], DNLP_GAMMA10_WID), DNLP_GAMMA10_BIT, DNLP_GAMMA10_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_02  , ve_reg_limit(p->gamma[11], DNLP_GAMMA11_WID), DNLP_GAMMA11_BIT, DNLP_GAMMA11_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_03  , ve_reg_limit(p->gamma[12], DNLP_GAMMA12_WID), DNLP_GAMMA12_BIT, DNLP_GAMMA12_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_03  , ve_reg_limit(p->gamma[13], DNLP_GAMMA13_WID), DNLP_GAMMA13_BIT, DNLP_GAMMA13_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_03  , ve_reg_limit(p->gamma[14], DNLP_GAMMA14_WID), DNLP_GAMMA14_BIT, DNLP_GAMMA14_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_03  , ve_reg_limit(p->gamma[15], DNLP_GAMMA15_WID), DNLP_GAMMA15_BIT, DNLP_GAMMA15_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_04  , ve_reg_limit(p->gamma[16], DNLP_GAMMA16_WID), DNLP_GAMMA16_BIT, DNLP_GAMMA16_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_04  , ve_reg_limit(p->gamma[17], DNLP_GAMMA17_WID), DNLP_GAMMA17_BIT, DNLP_GAMMA17_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_04  , ve_reg_limit(p->gamma[18], DNLP_GAMMA18_WID), DNLP_GAMMA18_BIT, DNLP_GAMMA18_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_04  , ve_reg_limit(p->gamma[19], DNLP_GAMMA19_WID), DNLP_GAMMA19_BIT, DNLP_GAMMA19_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_05  , ve_reg_limit(p->gamma[20], DNLP_GAMMA20_WID), DNLP_GAMMA20_BIT, DNLP_GAMMA20_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_05  , ve_reg_limit(p->gamma[21], DNLP_GAMMA21_WID), DNLP_GAMMA21_BIT, DNLP_GAMMA21_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_05  , ve_reg_limit(p->gamma[22], DNLP_GAMMA22_WID), DNLP_GAMMA22_BIT, DNLP_GAMMA22_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_05  , ve_reg_limit(p->gamma[23], DNLP_GAMMA23_WID), DNLP_GAMMA23_BIT, DNLP_GAMMA23_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_06  , ve_reg_limit(p->gamma[24], DNLP_GAMMA24_WID), DNLP_GAMMA24_BIT, DNLP_GAMMA24_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_06  , ve_reg_limit(p->gamma[25], DNLP_GAMMA25_WID), DNLP_GAMMA25_BIT, DNLP_GAMMA25_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_06  , ve_reg_limit(p->gamma[26], DNLP_GAMMA26_WID), DNLP_GAMMA26_BIT, DNLP_GAMMA26_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_06  , ve_reg_limit(p->gamma[27], DNLP_GAMMA27_WID), DNLP_GAMMA27_BIT, DNLP_GAMMA27_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_07  , ve_reg_limit(p->gamma[28], DNLP_GAMMA28_WID), DNLP_GAMMA28_BIT, DNLP_GAMMA28_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_07  , ve_reg_limit(p->gamma[29], DNLP_GAMMA29_WID), DNLP_GAMMA29_BIT, DNLP_GAMMA29_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_07  , ve_reg_limit(p->gamma[30], DNLP_GAMMA30_WID), DNLP_GAMMA30_BIT, DNLP_GAMMA30_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_07  , ve_reg_limit(p->gamma[31], DNLP_GAMMA31_WID), DNLP_GAMMA31_BIT, DNLP_GAMMA31_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_08  , ve_reg_limit(p->gamma[32], DNLP_GAMMA32_WID), DNLP_GAMMA32_BIT, DNLP_GAMMA32_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_08  , ve_reg_limit(p->gamma[33], DNLP_GAMMA33_WID), DNLP_GAMMA33_BIT, DNLP_GAMMA33_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_08  , ve_reg_limit(p->gamma[34], DNLP_GAMMA34_WID), DNLP_GAMMA34_BIT, DNLP_GAMMA34_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_08  , ve_reg_limit(p->gamma[35], DNLP_GAMMA35_WID), DNLP_GAMMA35_BIT, DNLP_GAMMA35_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_09  , ve_reg_limit(p->gamma[36], DNLP_GAMMA36_WID), DNLP_GAMMA36_BIT, DNLP_GAMMA36_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_09  , ve_reg_limit(p->gamma[37], DNLP_GAMMA37_WID), DNLP_GAMMA37_BIT, DNLP_GAMMA37_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_09  , ve_reg_limit(p->gamma[38], DNLP_GAMMA38_WID), DNLP_GAMMA38_BIT, DNLP_GAMMA38_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_09  , ve_reg_limit(p->gamma[39], DNLP_GAMMA39_WID), DNLP_GAMMA39_BIT, DNLP_GAMMA39_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_10  , ve_reg_limit(p->gamma[40], DNLP_GAMMA40_WID), DNLP_GAMMA40_BIT, DNLP_GAMMA40_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_10  , ve_reg_limit(p->gamma[41], DNLP_GAMMA41_WID), DNLP_GAMMA41_BIT, DNLP_GAMMA41_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_10  , ve_reg_limit(p->gamma[42], DNLP_GAMMA42_WID), DNLP_GAMMA42_BIT, DNLP_GAMMA42_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_10  , ve_reg_limit(p->gamma[43], DNLP_GAMMA43_WID), DNLP_GAMMA43_BIT, DNLP_GAMMA43_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_11  , ve_reg_limit(p->gamma[44], DNLP_GAMMA44_WID), DNLP_GAMMA44_BIT, DNLP_GAMMA44_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_11  , ve_reg_limit(p->gamma[45], DNLP_GAMMA45_WID), DNLP_GAMMA45_BIT, DNLP_GAMMA45_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_11  , ve_reg_limit(p->gamma[46], DNLP_GAMMA46_WID), DNLP_GAMMA46_BIT, DNLP_GAMMA46_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_11  , ve_reg_limit(p->gamma[47], DNLP_GAMMA47_WID), DNLP_GAMMA47_BIT, DNLP_GAMMA47_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_12  , ve_reg_limit(p->gamma[48], DNLP_GAMMA48_WID), DNLP_GAMMA48_BIT, DNLP_GAMMA48_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_12  , ve_reg_limit(p->gamma[49], DNLP_GAMMA49_WID), DNLP_GAMMA49_BIT, DNLP_GAMMA49_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_12  , ve_reg_limit(p->gamma[50], DNLP_GAMMA50_WID), DNLP_GAMMA50_BIT, DNLP_GAMMA50_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_12  , ve_reg_limit(p->gamma[51], DNLP_GAMMA51_WID), DNLP_GAMMA51_BIT, DNLP_GAMMA51_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_13  , ve_reg_limit(p->gamma[52], DNLP_GAMMA52_WID), DNLP_GAMMA52_BIT, DNLP_GAMMA52_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_13  , ve_reg_limit(p->gamma[53], DNLP_GAMMA53_WID), DNLP_GAMMA53_BIT, DNLP_GAMMA53_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_13  , ve_reg_limit(p->gamma[54], DNLP_GAMMA54_WID), DNLP_GAMMA54_BIT, DNLP_GAMMA54_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_13  , ve_reg_limit(p->gamma[55], DNLP_GAMMA55_WID), DNLP_GAMMA55_BIT, DNLP_GAMMA55_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_14  , ve_reg_limit(p->gamma[56], DNLP_GAMMA56_WID), DNLP_GAMMA56_BIT, DNLP_GAMMA56_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_14  , ve_reg_limit(p->gamma[57], DNLP_GAMMA57_WID), DNLP_GAMMA57_BIT, DNLP_GAMMA57_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_14  , ve_reg_limit(p->gamma[58], DNLP_GAMMA58_WID), DNLP_GAMMA58_BIT, DNLP_GAMMA58_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_14  , ve_reg_limit(p->gamma[59], DNLP_GAMMA59_WID), DNLP_GAMMA59_BIT, DNLP_GAMMA59_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_15  , ve_reg_limit(p->gamma[60], DNLP_GAMMA60_WID), DNLP_GAMMA60_BIT, DNLP_GAMMA60_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_15  , ve_reg_limit(p->gamma[61], DNLP_GAMMA61_WID), DNLP_GAMMA61_BIT, DNLP_GAMMA61_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_15  , ve_reg_limit(p->gamma[62], DNLP_GAMMA62_WID), DNLP_GAMMA62_BIT, DNLP_GAMMA62_WID);
    WRITE_CBUS_REG_BITS(VPP_DNLP_CTRL_15  , ve_reg_limit(p->gamma[63], DNLP_GAMMA63_WID), DNLP_GAMMA63_BIT, DNLP_GAMMA63_WID);
}


void ve_set_hsvs(struct ve_hsvs_s *p)
{
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, ve_reg_limit(p->en                , HSVS_EN_WID), HSVS_EN_BIT           , HSVS_EN_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_HGAIN , ve_reg_limit(p->peak_gain_h1      , PEAK_GAIN_H1_WID), PEAK_GAIN_H1_BIT      , PEAK_GAIN_H1_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_HGAIN , ve_reg_limit(p->peak_gain_h2      , PEAK_GAIN_H2_WID), PEAK_GAIN_H2_BIT      , PEAK_GAIN_H2_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_HGAIN , ve_reg_limit(p->peak_gain_h3      , PEAK_GAIN_H3_WID), PEAK_GAIN_H3_BIT      , PEAK_GAIN_H3_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_HGAIN , ve_reg_limit(p->peak_gain_h4      , PEAK_GAIN_H4_WID), PEAK_GAIN_H4_BIT      , PEAK_GAIN_H4_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_HGAIN , ve_reg_limit(p->peak_gain_h5      , PEAK_GAIN_H5_WID), PEAK_GAIN_H5_BIT      , PEAK_GAIN_H5_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_VGAIN , ve_reg_limit(p->peak_gain_v1      , PEAK_GAIN_V1_WID), PEAK_GAIN_V1_BIT      , PEAK_GAIN_V1_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_VGAIN , ve_reg_limit(p->peak_gain_v2      , PEAK_GAIN_V2_WID), PEAK_GAIN_V2_BIT      , PEAK_GAIN_V2_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_VGAIN , ve_reg_limit(p->peak_gain_v3      , PEAK_GAIN_V3_WID), PEAK_GAIN_V3_BIT      , PEAK_GAIN_V3_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_VGAIN , ve_reg_limit(p->peak_gain_v4      , PEAK_GAIN_V4_WID), PEAK_GAIN_V4_BIT      , PEAK_GAIN_V4_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_VGAIN , ve_reg_limit(p->peak_gain_v5      , PEAK_GAIN_V5_WID), PEAK_GAIN_V5_BIT      , PEAK_GAIN_V5_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_VGAIN , ve_reg_limit(p->peak_gain_v6      , PEAK_GAIN_V6_WID), PEAK_GAIN_V6_BIT      , PEAK_GAIN_V6_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_1 , ve_reg_limit(p->hpeak_slope1      , HPEAK_SLOPE1_WID), HPEAK_SLOPE1_BIT      , HPEAK_SLOPE1_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_1 , ve_reg_limit(p->hpeak_slope2      , HPEAK_SLOPE2_WID), HPEAK_SLOPE2_BIT      , HPEAK_SLOPE2_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_1 , ve_reg_limit(p->hpeak_thr1        , HPEAK_THR1_WID), HPEAK_THR1_BIT        , HPEAK_THR1_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_2 , ve_reg_limit(p->hpeak_thr2        , HPEAK_THR2_WID), HPEAK_THR2_BIT        , HPEAK_THR2_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_2 , ve_reg_limit(p->hpeak_nlp_cor_thr , HPEAK_NLP_COR_THR_WID), HPEAK_NLP_COR_THR_BIT , HPEAK_NLP_COR_THR_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_2 , ve_reg_limit(p->hpeak_nlp_gain_pos, HPEAK_NLP_GAIN_POS_WID), HPEAK_NLP_GAIN_POS_BIT, HPEAK_NLP_GAIN_POS_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_2 , ve_reg_limit(p->hpeak_nlp_gain_neg, HPEAK_NLP_GAIN_NEG_WID), HPEAK_NLP_GAIN_NEG_BIT, HPEAK_NLP_GAIN_NEG_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_1 , ve_reg_limit(p->vpeak_slope1      , VPEAK_SLOPE1_WID), VPEAK_SLOPE1_BIT      , VPEAK_SLOPE1_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_1 , ve_reg_limit(p->vpeak_slope2      , VPEAK_SLOPE2_WID), VPEAK_SLOPE2_BIT      , VPEAK_SLOPE2_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_3 , ve_reg_limit(p->vpeak_thr1        , VPEAK_THR1_WID), VPEAK_THR1_BIT        , VPEAK_THR1_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_4 , ve_reg_limit(p->vpeak_thr2        , VPEAK_THR2_WID), VPEAK_THR2_BIT        , VPEAK_THR2_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_4 , ve_reg_limit(p->vpeak_nlp_cor_thr , VPEAK_NLP_COR_THR_WID), VPEAK_NLP_COR_THR_BIT , VPEAK_NLP_COR_THR_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_4 , ve_reg_limit(p->vpeak_nlp_gain_pos, VPEAK_NLP_GAIN_POS_WID), VPEAK_NLP_GAIN_POS_BIT, VPEAK_NLP_GAIN_POS_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_4 , ve_reg_limit(p->vpeak_nlp_gain_neg, VPEAK_NLP_GAIN_NEG_WID), VPEAK_NLP_GAIN_NEG_BIT, VPEAK_NLP_GAIN_NEG_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_3 , ve_reg_limit(p->speak_slope1      , SPEAK_SLOPE1_WID), SPEAK_SLOPE1_BIT      , SPEAK_SLOPE1_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_3 , ve_reg_limit(p->speak_slope2      , SPEAK_SLOPE2_WID), SPEAK_SLOPE2_BIT      , SPEAK_SLOPE2_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_3 , ve_reg_limit(p->speak_thr1        , SPEAK_THR1_WID), SPEAK_THR1_BIT        , SPEAK_THR1_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_5 , ve_reg_limit(p->speak_thr2        , SPEAK_THR2_WID), SPEAK_THR2_BIT        , SPEAK_THR2_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_5 , ve_reg_limit(p->speak_nlp_cor_thr , SPEAK_NLP_COR_THR_WID), SPEAK_NLP_COR_THR_BIT , SPEAK_NLP_COR_THR_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_5 , ve_reg_limit(p->speak_nlp_gain_pos, SPEAK_NLP_GAIN_POS_WID), SPEAK_NLP_GAIN_POS_BIT, SPEAK_NLP_GAIN_POS_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_5 , ve_reg_limit(p->speak_nlp_gain_neg, SPEAK_NLP_GAIN_NEG_WID), SPEAK_NLP_GAIN_NEG_BIT, SPEAK_NLP_GAIN_NEG_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_NLP_3 , ve_reg_limit(p->peak_cor_gain     , PEAK_COR_GAIN_WID), PEAK_COR_GAIN_BIT     , PEAK_COR_GAIN_WID);
    WRITE_CBUS_REG_BITS(VPP_SHARP_LIMIT    , ve_reg_limit(p->peak_cor_thr_l   , PEAK_COR_THR_L_WID), PEAK_COR_THR_L_BIT    , PEAK_COR_THR_L_WID);
    WRITE_CBUS_REG_BITS(VPP_SHARP_LIMIT    , ve_reg_limit(p->peak_cor_thr_h   , PEAK_COR_THR_H_WID), PEAK_COR_THR_H_BIT    , PEAK_COR_THR_H_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_HGAIN , ve_reg_limit(p->vlti_step         , VLTI_STEP_WID), VLTI_STEP_BIT         , VLTI_STEP_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_HGAIN , ve_reg_limit(p->vlti_step2        , VLTI_STEP2_WID), VLTI_STEP2_BIT        , VLTI_STEP2_WID);
    WRITE_CBUS_REG_BITS(VPP_VLTI_CTRL     , ve_reg_limit(p->vlti_thr          , VLTI_THR_WID), VLTI_THR_BIT          , VLTI_THR_WID);
    WRITE_CBUS_REG_BITS(VPP_VLTI_CTRL     , ve_reg_limit(p->vlti_gain_pos     , VLTI_GAIN_POS_WID), VLTI_GAIN_POS_BIT     , VLTI_GAIN_POS_WID);
    WRITE_CBUS_REG_BITS(VPP_VLTI_CTRL     , ve_reg_limit(p->vlti_gain_neg     , VLTI_GAIN_NEG_WID), VLTI_GAIN_NEG_BIT     , VLTI_GAIN_NEG_WID);
    WRITE_CBUS_REG_BITS(VPP_VLTI_CTRL     , ve_reg_limit(p->vlti_blend_factor , VLTI_BLEND_FACTOR_WID), VLTI_BLEND_FACTOR_BIT , VLTI_BLEND_FACTOR_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_HGAIN , ve_reg_limit(p->hlti_step         , HLTI_STEP_WID), HLTI_STEP_BIT         , HLTI_STEP_WID);
    WRITE_CBUS_REG_BITS(VPP_HLTI_CTRL     , ve_reg_limit(p->hlti_thr          , HLTI_THR_WID), HLTI_THR_BIT          , HLTI_THR_WID);
    WRITE_CBUS_REG_BITS(VPP_HLTI_CTRL     , ve_reg_limit(p->hlti_gain_pos     , HLTI_GAIN_POS_WID), HLTI_GAIN_POS_BIT     , HLTI_GAIN_POS_WID);
    WRITE_CBUS_REG_BITS(VPP_HLTI_CTRL     , ve_reg_limit(p->hlti_gain_neg     , HLTI_GAIN_NEG_WID), HLTI_GAIN_NEG_BIT     , HLTI_GAIN_NEG_WID);
    WRITE_CBUS_REG_BITS(VPP_HLTI_CTRL     , ve_reg_limit(p->hlti_blend_factor , HLTI_BLEND_FACTOR_WID), HLTI_BLEND_FACTOR_BIT , HLTI_BLEND_FACTOR_WID);
    WRITE_CBUS_REG_BITS(VPP_SHARP_LIMIT    , ve_reg_limit(p->vlimit_coef_h    , VLIMIT_COEF_H_WID), VLIMIT_COEF_H_BIT     , VLIMIT_COEF_H_WID);
    WRITE_CBUS_REG_BITS(VPP_SHARP_LIMIT    , ve_reg_limit(p->vlimit_coef_l    , VLIMIT_COEF_L_WID), VLIMIT_COEF_L_BIT     , VLIMIT_COEF_L_WID);
    WRITE_CBUS_REG_BITS(VPP_SHARP_LIMIT    , ve_reg_limit(p->hlimit_coef_h    , HLIMIT_COEF_H_WID), HLIMIT_COEF_H_BIT     , HLIMIT_COEF_H_WID);
    WRITE_CBUS_REG_BITS(VPP_SHARP_LIMIT    , ve_reg_limit(p->hlimit_coef_l    , HLIMIT_COEF_L_WID), HLIMIT_COEF_L_BIT     , HLIMIT_COEF_L_WID);
    WRITE_CBUS_REG_BITS(VPP_CTI_CTRL      , ve_reg_limit(p->cti_444_422_en    , CTI_C444TO422_EN_WID), CTI_C444TO422_EN_BIT  , CTI_C444TO422_EN_WID);
    WRITE_CBUS_REG_BITS(VPP_CTI_CTRL      , ve_reg_limit(p->cti_422_444_en    , CTI_C422TO444_EN_WID), CTI_C422TO444_EN_BIT  , CTI_C422TO444_EN_WID);
    WRITE_CBUS_REG_BITS(VPP_CTI_CTRL      , ve_reg_limit(p->cti_blend_factor  , CTI_BLEND_FACTOR_WID), CTI_BLEND_FACTOR_BIT  , CTI_BLEND_FACTOR_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_VGAIN , ve_reg_limit(p->vcti_buf_en       , VCTI_BUF_EN_WID), VCTI_BUF_EN_BIT       , VCTI_BUF_EN_WID);
    WRITE_CBUS_REG_BITS(VPP_PEAKING_VGAIN , ve_reg_limit(p->vcti_buf_mode_c5l , VCTI_BUF_MODE_C5L_WID), VCTI_BUF_MODE_C5L_BIT , VCTI_BUF_MODE_C5L_WID);
    WRITE_CBUS_REG_BITS(VPP_CTI_CTRL      , ve_reg_limit(p->vcti_filter       , VCTI_FILTER_WID), VCTI_FILTER_BIT       , VCTI_FILTER_WID);
    WRITE_CBUS_REG_BITS(VPP_CTI_CTRL      , ve_reg_limit(p->hcti_step         , HCTI_STEP_WID), HCTI_STEP_BIT         , HCTI_STEP_WID);
    WRITE_CBUS_REG_BITS(VPP_CTI_CTRL      , ve_reg_limit(p->hcti_step2        , HCTI_STEP2_WID), HCTI_STEP2_BIT        , HCTI_STEP2_WID);
    WRITE_CBUS_REG_BITS(VPP_CTI_CTRL      , ve_reg_limit(p->hcti_thr          , HCTI_THR_WID), HCTI_THR_BIT          , HCTI_THR_WID);
    WRITE_CBUS_REG_BITS(VPP_CTI_CTRL      , ve_reg_limit(p->hcti_gain         , HCTI_GAIN_WID), HCTI_GAIN_BIT         , HCTI_GAIN_WID);
    WRITE_CBUS_REG_BITS(VPP_CTI_CTRL      , ve_reg_limit(p->hcti_mode_median  , HCTI_MODE_MEDIAN_WID), HCTI_MODE_MEDIAN_BIT  , HCTI_MODE_MEDIAN_WID);
}


void ve_set_ccor(struct ve_ccor_s *p)
{
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, ve_reg_limit(p->en   , CCOR_EN_WID), CCOR_EN_BIT   , CCOR_EN_WID);
    WRITE_CBUS_REG_BITS(VPP_CCORING_CTRL  , ve_reg_limit(p->slope, CCOR_SLOPE_WID), CCOR_SLOPE_BIT, CCOR_SLOPE_WID);
    WRITE_CBUS_REG_BITS(VPP_CCORING_CTRL  , ve_reg_limit(p->thr  , CCOR_THR_WID), CCOR_THR_BIT  , CCOR_THR_WID);
}


void ve_set_benh(struct ve_benh_s *p)
{
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL, ve_reg_limit(p->en                 , BENH_EN_WID)         , BENH_EN_BIT           , BENH_EN_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_1, ve_reg_limit(p->cb_inc             , BENH_CB_INC_WID)     , BENH_CB_INC_BIT       , BENH_CB_INC_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_1, ve_reg_limit(p->cr_inc             , BENH_CR_INC_WID)     , BENH_CR_INC_BIT       , BENH_CR_INC_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_1, ve_reg_limit(p->gain_cr            , BENH_GAIN_CR_WID)    , BENH_GAIN_CR_BIT      , BENH_GAIN_CR_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_1, ve_reg_limit(p->gain_cb4cr         , BENH_GAIN_CB4CR_WID) , BENH_GAIN_CB4CR_BIT   , BENH_GAIN_CB4CR_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_1, ve_reg_limit(p->luma_h             , BENH_LUMA_H_WID)     , BENH_LUMA_H_BIT       , BENH_LUMA_H_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_2, ve_reg_limit(p->err_crp            , BENH_ERR_CRP_WID)    , BENH_ERR_CRP_BIT      , BENH_ERR_CRP_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_2, ve_reg_limit(p->err_crn            , BENH_ERR_CRN_WID)    , BENH_ERR_CRN_BIT      , BENH_ERR_CRN_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_3, ve_reg_limit(p->err_cbp            , BENH_ERR_CBP_WID)    , BENH_ERR_CBP_BIT      , BENH_ERR_CBP_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_3, ve_reg_limit(p->err_cbn            , BENH_ERR_CBN_WID)    , BENH_ERR_CBN_BIT      , BENH_ERR_CBN_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_1, ve_benh_inv[ve_reg_limit(p->err_crp, BENH_ERR_CRP_WID)][1], BENH_ERR_CRP_INV_H_BIT, BENH_ERR_CRP_INV_H_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_1, ve_benh_inv[ve_reg_limit(p->err_crn, BENH_ERR_CRN_WID)][1], BENH_ERR_CRN_INV_H_BIT, BENH_ERR_CRN_INV_H_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_1, ve_benh_inv[ve_reg_limit(p->err_cbp, BENH_ERR_CBP_WID)][1], BENH_ERR_CBP_INV_H_BIT, BENH_ERR_CBP_INV_H_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_1, ve_benh_inv[ve_reg_limit(p->err_cbn, BENH_ERR_CBN_WID)][1], BENH_ERR_CBN_INV_H_BIT, BENH_ERR_CBN_INV_H_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_2, ve_benh_inv[ve_reg_limit(p->err_crp, BENH_ERR_CRP_WID)][0], BENH_ERR_CRP_INV_L_BIT, BENH_ERR_CRP_INV_L_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_2, ve_benh_inv[ve_reg_limit(p->err_crn, BENH_ERR_CRN_WID)][0], BENH_ERR_CRN_INV_L_BIT, BENH_ERR_CRN_INV_L_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_3, ve_benh_inv[ve_reg_limit(p->err_cbp, BENH_ERR_CBP_WID)][0], BENH_ERR_CBP_INV_L_BIT, BENH_ERR_CBP_INV_L_WID);
    WRITE_CBUS_REG_BITS(VPP_BLUE_STRETCH_3, ve_benh_inv[ve_reg_limit(p->err_cbn, BENH_ERR_CBN_WID)][0], BENH_ERR_CBN_INV_L_BIT, BENH_ERR_CBN_INV_L_WID);
}


void ve_set_demo(struct ve_demo_s *p)
{
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL           , ve_reg_limit(p->bext, DEMO_BEXT_WID), DEMO_BEXT_BIT, DEMO_BEXT_WID);
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL           , ve_reg_limit(p->dnlp, DEMO_DNLP_WID), DEMO_DNLP_BIT, DEMO_DNLP_WID);
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL           , ve_reg_limit(p->hsvs, DEMO_HSVS_WID), DEMO_HSVS_BIT, DEMO_HSVS_WID);
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL           , ve_reg_limit(p->ccor, DEMO_CCOR_WID), DEMO_CCOR_BIT, DEMO_CCOR_WID);
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL           , ve_reg_limit(p->benh, DEMO_BENH_WID), DEMO_BENH_BIT, DEMO_BENH_WID);
    WRITE_CBUS_REG_BITS(VPP_VE_ENABLE_CTRL           , ve_reg_limit(p->pos , VE_DEMO_POS_WID), VE_DEMO_POS_BIT, VE_DEMO_POS_WID);
    WRITE_CBUS_REG_BITS(VPP_VE_DEMO_LEFT_SCREEN_WIDTH, ve_reg_limit(p->wid , DEMO_WID_WID), DEMO_WID_BIT , DEMO_WID_WID);
}


void ve_set_regs(struct ve_regs_s *p)
{
    if (!(p->mode)) { // read
        switch (p->port) {
        case 0:    // direct access
            p->val = READ_CBUS_REG_BITS(p->reg, p->bit, p->wid);
            break;
        case 1:    // reserved
            break;
        case 2:    // reserved
            break;
        case 3:    // reserved
            break;
        default:   // NA
            break;
        }
    } else {           // write
        switch (p->port) {
        case 0:    // direct access
            WRITE_CBUS_REG_BITS(p->reg, ve_reg_limit(p->val, p->wid), p->bit, p->wid);
            break;
        case 1:    // reserved
            break;
        case 2:    // reserved
            break;
        case 3:    // reserved
            break;
        default:   // NA
            break;
        }
    }
}

