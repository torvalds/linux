/*
 * Color Management
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
#include "linux/amlogic/amports/cm.h"

#include "cm_regs.h"
#include "amcm.h"

#if 0
struct cm_region_s cm_region;
struct cm_top_s    cm_top;
struct cm_demo_s   cm_demo;
#endif

static inline ulong read_cm_reg_bits(ulong reg, ulong bit, ulong wid)
{
    WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, reg);
    return((READ_CBUS_REG(VPP_CHROMA_DATA_PORT) >> bit) & ((1 << wid) - 1));
}

static inline void write_cm_reg_bits(ulong reg, ulong val, ulong bit, ulong wid)
{
    ulong mask = (1 << wid) - 1;
    ulong data = read_cm_reg_bits(reg, 0, 31);

    if (val > mask) {
        val = mask;
    }
    data &= ~(mask << bit);
    data |= (val << bit);
    WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, reg);
    WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, val);
}

// ***************************************************************************
// *** IOCTL-oriented functions *********************************************
// ***************************************************************************

void cm_set_region(struct cm_region_s *p)
{
    ulong reg_off         = (p->region_idx) * 6;
    ulong hue_shf_ran_inv = ((1 << 20) / (p->hue_shf_ran) + 1) >> 1;

    write_cm_reg_bits(HUE_HUE_RANGE_REG00 + reg_off, p->sym_en         , SYM_EN_BIT         , SYM_EN_WID);
    write_cm_reg_bits(CHROMA_GAIN_REG00  + reg_off, p->sat_en         , SAT_EN_BIT         , SAT_EN_WID);
    write_cm_reg_bits(CHROMA_GAIN_REG00  + reg_off, p->sat_central_en , SAT_CENTRAL_EN_BIT , SAT_CENTRAL_EN_WID);
    write_cm_reg_bits(CHROMA_GAIN_REG00  + reg_off, p->sat_shape      , SAT_SHAPE_BIT      , SAT_SHAPE_WID);
    write_cm_reg_bits(CHROMA_GAIN_REG00  + reg_off, p->sat_gain       , SAT_GAIN_BIT       , SAT_GAIN_WID);
    write_cm_reg_bits(CHROMA_GAIN_REG00  + reg_off, p->sat_inc        , SAT_INC_BIT        , SAT_INC_WID);
    write_cm_reg_bits(SAT_SAT_RANGE_REG00 + reg_off, p->sat_lum_h_slope, SAT_LUM_H_SLOPE_BIT, SAT_LUM_H_SLOPE_WID);
    write_cm_reg_bits(SAT_SAT_RANGE_REG00 + reg_off, p->sat_lum_l_slope, SAT_LUM_L_SLOPE_BIT, SAT_LUM_L_SLOPE_WID);
    write_cm_reg_bits(HUE_SAT_RANGE_REG00 + reg_off, p->sat_lum_h      , SAT_LUM_H_BIT      , SAT_LUM_H_WID);
    write_cm_reg_bits(HUE_LUM_RANGE_REG00 + reg_off, p->sat_lum_l      , SAT_LUM_L_BIT      , SAT_LUM_L_WID);
    write_cm_reg_bits(SAT_SAT_RANGE_REG00 + reg_off, p->sat_sat_h_slope, SAT_SAT_H_SLOPE_BIT, SAT_SAT_H_SLOPE_WID);
    write_cm_reg_bits(SAT_SAT_RANGE_REG00 + reg_off, p->sat_sat_l_slope, SAT_SAT_L_SLOPE_BIT, SAT_SAT_L_SLOPE_WID);
    write_cm_reg_bits(SAT_SAT_RANGE_REG00 + reg_off, p->sat_sat_h      , SAT_SAT_H_BIT      , SAT_SAT_H_WID);
    write_cm_reg_bits(SAT_SAT_RANGE_REG00 + reg_off, p->sat_sat_l      , SAT_SAT_L_BIT      , SAT_SAT_L_WID);
    write_cm_reg_bits(CHROMA_GAIN_REG00  + reg_off, p->hue_en         , HUE_EN_BIT         , HUE_EN_WID);
    write_cm_reg_bits(CHROMA_GAIN_REG00  + reg_off, p->hue_central_en , HUE_CENTRAL_EN_BIT , HUE_CENTRAL_EN_WID);
    write_cm_reg_bits(CHROMA_GAIN_REG00  + reg_off, p->hue_shape      , HUE_SHAPE_BIT      , HUE_SHAPE_WID);
    write_cm_reg_bits(CHROMA_GAIN_REG00  + reg_off, p->hue_gain       , HUE_GAIN_BIT       , HUE_GAIN_WID);
    write_cm_reg_bits(CHROMA_GAIN_REG00  + reg_off, p->hue_clockwise  , HUE_CLOCKWISE_BIT  , HUE_CLOCKWISE_WID);
    write_cm_reg_bits(HUE_HUE_RANGE_REG00 + reg_off, p->hue_shf_ran    , HUE_SHF_RAN_BIT    , HUE_SHF_RAN_WID);
    write_cm_reg_bits(HUE_RANGE_INV_REG00 + reg_off, hue_shf_ran_inv   , HUE_SHF_RAN_INV_BIT, HUE_SHF_RAN_INV_WID);
    write_cm_reg_bits(HUE_HUE_RANGE_REG00 + reg_off, p->hue_shf_sta    , HUE_SHF_STA_BIT    , HUE_SHF_STA_WID);
    write_cm_reg_bits(HUE_LUM_RANGE_REG00 + reg_off, p->hue_lum_h_slope, HUE_LUM_H_SLOPE_BIT, HUE_LUM_H_SLOPE_WID);
    write_cm_reg_bits(HUE_LUM_RANGE_REG00 + reg_off, p->hue_lum_l_slope, HUE_LUM_L_SLOPE_BIT, HUE_LUM_L_SLOPE_WID);
    write_cm_reg_bits(HUE_LUM_RANGE_REG00 + reg_off, p->hue_lum_h      , HUE_LUM_H_BIT      , HUE_LUM_H_WID);
    write_cm_reg_bits(HUE_LUM_RANGE_REG00 + reg_off, p->hue_lum_l      , HUE_LUM_L_BIT      , HUE_LUM_L_WID);
    write_cm_reg_bits(HUE_SAT_RANGE_REG00 + reg_off, p->hue_sat_h_slope, HUE_SAT_H_SLOPE_BIT, HUE_SAT_H_SLOPE_WID);
    write_cm_reg_bits(HUE_SAT_RANGE_REG00 + reg_off, p->hue_sat_l_slope, HUE_SAT_L_SLOPE_BIT, HUE_SAT_L_SLOPE_WID);
    write_cm_reg_bits(HUE_SAT_RANGE_REG00 + reg_off, p->hue_sat_h      , HUE_SAT_H_BIT      , HUE_SAT_H_WID);
    write_cm_reg_bits(HUE_SAT_RANGE_REG00 + reg_off, p->hue_sat_l      , HUE_SAT_L_BIT      , HUE_SAT_L_WID);
}

void cm_set_top(struct cm_top_s *p)
{
    write_cm_reg_bits(REG_CHROMA_CONTROL, p->chroma_en    , CHROMA_EN_BIT    , CHROMA_EN_WID);
    write_cm_reg_bits(REG_CHROMA_CONTROL, p->sat_sel      , SAT_SEL_BIT      , SAT_SEL_WID);
    write_cm_reg_bits(REG_CHROMA_CONTROL, p->uv_adj_en    , UV_ADJ_EN_BIT    , UV_ADJ_EN_WID);
    write_cm_reg_bits(REG_CHROMA_CONTROL, p->rgb_to_hue_en, RGB_TO_HUE_EN_BIT, RGB_TO_HUE_EN_WID);
    write_cm_reg_bits(REG_CHROMA_CONTROL, p->csc_sel      , CSC_SEL_BIT      , CSC_SEL_WID);
}

void cm_set_demo(struct cm_demo_s *p)
{
    write_cm_reg_bits(REG_CHROMA_CONTROL, p->en        , DEMO_EN_BIT        , DEMO_EN_WID);
    write_cm_reg_bits(REG_CHROMA_CONTROL, p->pos       , CM_DEMO_POS_BIT    , CM_DEMO_POS_WID);
    write_cm_reg_bits(REG_CHROMA_CONTROL, p->hlight_adj, DEMO_HLIGHT_ADJ_BIT, DEMO_HLIGHT_ADJ_WID);
    write_cm_reg_bits(REG_CHROMA_CONTROL, p->wid       , CM_DEMO_WID_BIT       , CM_DEMO_WID_WID);
}

void cm_set_regs(struct cm_regs_s *p)
{
    if (!(p->mode)) { // read
        switch (p->port) {
        case 0:    // reserved
            break;
        case 1:    // CM port registers
            p->val = read_cm_reg_bits(p->reg, p->bit, p->wid);
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
        case 0:    // reserved
            break;
        case 1:    // CM port registers
            write_cm_reg_bits(p->reg, p->val, p->bit, p->wid);
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

