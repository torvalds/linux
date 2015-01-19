/*
 * Amlogic M6
 * frame buffer driver  -------it660x input
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

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <asm/delay.h>
#include <asm/atomic.h>

#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#include <mach/am_regs.h>

#include "../tvin_frontend.h"
#include "dvin.h"

#define DEVICE_NAME "t660xin"
#define MODULE_NAME "it660xin"


typedef struct it660xin_s{
    unsigned char       fmt_check_cnt;
    unsigned char       watch_dog;

    unsigned        dec_status : 1;
    unsigned        wrap_flag : 1;
    /*add for tv frontend architecture*/
    struct tvin_frontend_s frontend;
    struct vdin_parm_s parm;

}it660xin_t;

static int it660xin_support(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
        if(port == TVIN_PORT_DVIN0)
                return 0;
        else
                return -1;
}
static void it660xin_open(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
        it660xin_t *devp = container_of(fe,it660xin_t,frontend);
        /*copy the vdin_parm_s to local device parameter*/
        if(!memcpy(&devp->parm,fe->private_data,sizeof(vdin_parm_t))){
                printk("[it660..]%s copy vdin parm error.\n",__func__);
        }
        /*do something init the it660 device*/
        WRITE_MPEG_REG(PERIPHS_PIN_MUX_0, READ_MPEG_REG(PERIPHS_PIN_MUX_0)|
                                        ((1 << 10)    | // pm_gpioA_lcd_in_de
                                        (1 << 9)     | // pm_gpioA_lcd_in_vs
                                        (1 << 8)     | // pm_gpioA_lcd_in_hs
                                        (1 << 7)     | // pm_gpioA_lcd_in_clk
                                        (1 << 6)));     // pm_gpioA_lcd_in

        WRITE_MPEG_REG_BITS(VDIN_ASFIFO_CTRL2, 0x39, 2, 6); 

}
static void it660xin_close(struct tvin_frontend_s *fe)
{
        it660xin_t *devp = container_of(fe,it660xin_t,frontend);
                
}
static void it660xin_start(struct tvin_frontend_s *fe, enum tvin_sig_fmt_e fmt)
{
        it660xin_t *devp = container_of(fe,it660xin_t,frontend);
                
}
static void it660xin_stop(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
        it660xin_t *devp = container_of(fe,it660xin_t,frontend);
                
}
static int it660xin_isr(struct tvin_frontend_s *fe, unsigned int hcnt64)
{
        it660xin_t *devp = container_of(fe,it660xin_t,frontend);
                return 0;
}

static struct tvin_decoder_ops_s it660xin_dec_ops = {
        .support = it660xin_support,
        .open = it660xin_open,
        .close = it660xin_close,
        .start  = it660xin_start,
        .stop  = it660xin_stop,
        .decode_isr =it660xin_isr, 
};

static void it660xin_get_sig_propery(struct tvin_frontend_s *fe, struct tvin_sig_property_s *prop)
{
        it660xin_t *devp = container_of(fe,it660xin_t,frontend);
        prop->color_format = TVIN_RGB444;
        if(devp->parm.h_active >= 1440)
                prop->decimation_ratio = 1;
        else 
                prop->decimation_ratio = 0;
}
static struct tvin_state_machine_ops_s it660xin_sm_ops = {
        .get_sig_propery = it660xin_get_sig_propery,
};
static int it660xin_probe(struct platform_device *pdev)
{
    int ret = 0;
    it660xin_t *it660_devp = kmalloc(sizeof(it660xin_t),GFP_KERNEL);
    printk("[it660..] it660xin probe start.\n");
    if(!it660_devp){
        printk("[it660..] %s kmalloc error.\n",__func__);
        return -ENOMEM;
    }
    memset(it660_devp,0,sizeof(it660xin_t));
    /*init the it660xin frontend and register*/
    if(tvin_frontend_init(&it660_devp->frontend,&it660xin_dec_ops,&it660xin_sm_ops,0))
    {
        printk("[it660..] %s init it660 frontend error.\n",__func__);
        ret = -1;
    }
    if(tvin_reg_frontend(&it660_devp->frontend)){
         printk("[it660..] %s register it660 frontend error.\n",__func__);
        ret = -1;
    }
    platform_set_drvdata(pdev,it660_devp);
    
    printk("[it660..] it660xin probe end.\n");
    return ret;
}

static int it660xin_remove(struct platform_device *pdev)
{
    it660xin_t *it660_devp = platform_get_drvdata(pdev);
    /*unregister it660 frontend*/
        tvin_unreg_frontend(&it660_devp->frontend);
    return 0;
}



static struct platform_driver it660xin_driver = {
    .probe      = it660xin_probe,
    .remove     = it660xin_remove,
    .driver     = {
        .name   = DEVICE_NAME,
    }
};

static struct platform_device* it660xin_device = NULL;

static int __init it660xin_init_module(void)
{
    printk("it660xin module init\n");
        it660xin_device = platform_device_alloc(DEVICE_NAME,0);
    if (!it660xin_device) {
        printk("failed to alloc it660xin_device\n");
        return -ENOMEM;
    }

    if(platform_device_add(it660xin_device)){
        platform_device_put(it660xin_device);
        printk("failed to add it660xin_device\n");
        return -ENODEV;
    }
    if (platform_driver_register(&it660xin_driver)) {
        printk("failed to register it660xin driver\n");

        platform_device_del(it660xin_device);
        platform_device_put(it660xin_device);
        return -ENODEV;
    }
    return 0;
}

static void __exit it660xin_exit_module(void)
{
    printk("it660xin module remove.\n");
    platform_driver_unregister(&it660xin_driver);
    return ;
}

module_init(it660xin_init_module);
module_exit(it660xin_exit_module);


void config_dvin (unsigned long hs_pol_inv,             // Invert HS polarity, for HW regards HS active high.
                  unsigned long vs_pol_inv,             // Invert VS polarity, for HW regards VS active high.
                  unsigned long de_pol_inv,             // Invert DE polarity, for HW regards DE active high.
                  unsigned long field_pol_inv,          // Invert FIELD polarity, for HW regards odd field when high.
                  unsigned long ext_field_sel,          // FIELD source select:
                                                        // 1=Use external FIELD signal, ignore internal FIELD detection result;
                                                        // 0=Use internal FIELD detection result, ignore external input FIELD signal.
                  unsigned long de_mode,                // DE mode control:
                                                        // 0=Ignore input DE signal, use internal detection to to determine active pixel;
                                                        // 1=Rsrv;
                                                        // 2=During internal detected active region, if input DE goes low, replace input data with the last good data;
                                                        // 3=Active region is determined by input DE, no internal detection.
                  unsigned long data_comp_map,          // Map input data to form YCbCr.
                                                        // Use 0 if input is YCbCr;
                                                        // Use 1 if input is YCrCb;
                                                        // Use 2 if input is CbCrY;
                                                        // Use 3 if input is CbYCr;
                                                        // Use 4 if input is CrYCb;
                                                        // Use 5 if input is CrCbY;
                                                        // 6,7=Rsrv.
                  unsigned long mode_422to444,          // 422 to 444 conversion control:
                                                        // 0=No convertion; 1=Rsrv;
                                                        // 2=Convert 422 to 444, use previous C value;
                                                        // 3=Convert 422 to 444, use average C value.
                  unsigned long dvin_clk_inv,           // Invert dvin_clk_in for ease of data capture.
                  unsigned long vs_hs_tim_ctrl,         // Controls which edge of HS/VS (post polarity control) the active pixel/line is related:
                                                        // Bit 0: HS and active pixel relation.
                                                        //  0=Start of active pixel is counted from the rising edge of HS;
                                                        //  1=Start of active pixel is counted from the falling edge of HS;
                                                        // Bit 1: VS and active line relation.
                                                        //  0=Start of active line is counted from the rising edge of VS;
                                                        //  1=Start of active line is counted from the falling edge of VS.
                  unsigned long hs_lead_vs_odd_min,     // For internal FIELD detection:
                                                        // Minimum clock cycles allowed for HS active edge to lead before VS active edge in odd field. Failing it the field is even.
                  unsigned long hs_lead_vs_odd_max,     // For internal FIELD detection:
                                                        // Maximum clock cycles allowed for HS active edge to lead before VS active edge in odd field. Failing it the field is even.
                  unsigned long active_start_pix_fe,    // Number of clock cycles between HS active edge to first active pixel, in even field.
                  unsigned long active_start_pix_fo,    // Number of clock cycles between HS active edge to first active pixel, in odd field.
                  unsigned long active_start_line_fe,   // Number of clock cycles between VS active edge to first active line, in even field.
                  unsigned long active_start_line_fo,   // Number of clock cycles between VS active edge to first active line, in odd field.
                  unsigned long line_width,             // Number_of_pixels_per_line
                  unsigned long field_height)           // Number_of_lines_per_field
{
    unsigned long data32;

    printk("[it660..]%s: %lu %lu %lu %lu.\n",  __func__, active_start_pix_fe, active_start_line_fe,  line_width, field_height);  
    // Program reg DVIN_CTRL_STAT: disable DVIN
    WRITE_MPEG_REG(DVIN_CTRL_STAT, 0);

    // Program reg DVIN_FRONT_END_CTRL
    data32 = 0;
    data32 |= (hs_pol_inv       & 0x1)  << 0;
    data32 |= (vs_pol_inv       & 0x1)  << 1;
    data32 |= (de_pol_inv       & 0x1)  << 2;
    data32 |= (field_pol_inv    & 0x1)  << 3;
    data32 |= (ext_field_sel    & 0x1)  << 4;
    data32 |= (de_mode          & 0x3)  << 5;
    data32 |= (mode_422to444    & 0x3)  << 7;
    data32 |= (dvin_clk_inv     & 0x1)  << 9;
    data32 |= (vs_hs_tim_ctrl   & 0x3)  << 10;
    WRITE_MPEG_REG(DVIN_FRONT_END_CTRL, data32);
    
    // Program reg DVIN_HS_LEAD_VS_ODD
    data32 = 0;
    data32 |= (hs_lead_vs_odd_min & 0xfff) << 0;
    data32 |= (hs_lead_vs_odd_max & 0xfff) << 16;
    WRITE_MPEG_REG(DVIN_HS_LEAD_VS_ODD, data32);

    // Program reg DVIN_ACTIVE_START_PIX
    data32 = 0;
    data32 |= (active_start_pix_fe & 0xfff) << 0;
    data32 |= (active_start_pix_fo & 0xfff) << 16;
    WRITE_MPEG_REG(DVIN_ACTIVE_START_PIX, data32);
    
    // Program reg DVIN_ACTIVE_START_LINE
    data32 = 0;
    data32 |= (active_start_line_fe & 0xfff) << 0;
    data32 |= (active_start_line_fo & 0xfff) << 16;
    WRITE_MPEG_REG(DVIN_ACTIVE_START_LINE, data32);
    
    // Program reg DVIN_DISPLAY_SIZE
    data32 = 0;
    data32 |= ((line_width-1)   & 0xfff) << 0;
    data32 |= ((field_height-1) & 0xfff) << 16;
    WRITE_MPEG_REG(DVIN_DISPLAY_SIZE, data32);
    
    // Program reg DVIN_CTRL_STAT, and enable DVIN
    data32 = 0;
    data32 |= 1                     << 0;
    data32 |= (data_comp_map & 0x7) << 1;
    WRITE_MPEG_REG(DVIN_CTRL_STAT, data32);
} /* config_dvin */



