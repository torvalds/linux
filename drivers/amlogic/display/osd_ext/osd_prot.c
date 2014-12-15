/*
 * Amlogic Meson
 * frame buffer driver
 *
 * Copyright (C) 2009 Amlogic, Inc.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Amlogic Platform Group
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <plat/regops.h>
#include <mach/am_regs.h>
#include <linux/irqreturn.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/amlogic/osd/osd.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/amlogic/amports/canvas.h>
#include "osd_log.h"
#include <linux/amlogic/amlog.h>
#include "osd_prot.h"



int osd_ext_set_prot(unsigned char   x_rev,
                unsigned char   y_rev,
                unsigned char   bytes_per_pixel,
                unsigned char   conv_422to444,
                unsigned char   little_endian,
                unsigned int    hold_lines,
                unsigned int    x_start,
                unsigned int    x_end,
                unsigned int    y_start,
                unsigned int    y_end,
                unsigned int    y_len_m1,
                unsigned char   y_step,
                unsigned char   pat_start_ptr,
                unsigned char   pat_end_ptr,
                unsigned long   pat_val,
                unsigned int    canv_addr,
                unsigned int    cid_val,
                unsigned char   cid_mode,
                unsigned char   cugt,
                unsigned char   req_onoff_en,
                unsigned int    req_on_max,
                unsigned int    req_off_min,
                unsigned char   osd_index,
                unsigned char   on)
{
	unsigned long   data32;
	if(!on){
		aml_clr_reg32_mask(P_VPU_PROT1_MMC_CTRL,0xf<<12);  //no one use prot1.
		aml_clr_reg32_mask(P_VPU_PROT1_CLK_GATE, 1<<0);
		if(osd_index==OSD1){
			aml_set_reg32_bits(P_VIU2_OSD1_BLK0_CFG_W0, 1, 15, 1);//switch back to little endian
			aml_write_reg32(P_VIU2_OSD1_PROT_CTRL,0);
		}else if(osd_index==OSD2){
			aml_set_reg32_bits(P_VIU2_OSD2_BLK0_CFG_W0, 1, 15, 1);//switch back to little endian
			aml_write_reg32(P_VIU2_OSD2_PROT_CTRL,0);
		}
		
		return 0;
	}
	if(osd_index==OSD1){
		aml_set_reg32_bits(P_VPU_PROT1_MMC_CTRL, 4, 12, 4);//bit[12..15] OSD1 OSD2 OSD3 OSD4
		aml_write_reg32(P_VIU2_OSD1_PROT_CTRL,1<<15|y_len_m1);
		aml_clr_reg32_mask(P_VIU2_OSD1_BLK0_CFG_W0, 1<<15); //before rotate set big endian
	}else if(osd_index==OSD2){
		aml_set_reg32_bits(P_VPU_PROT1_MMC_CTRL, 8, 12, 4);//bit[12..15] OSD1 OSD2 OSD3 OSD4
		aml_write_reg32(P_VIU2_OSD2_PROT_CTRL,1<<15|y_len_m1);
		aml_clr_reg32_mask(P_VIU2_OSD2_BLK0_CFG_W0, 1<<15); //before rotate set big endian
	}

    data32  = (x_end    << 16)  |
              (x_start  << 0);
    aml_write_reg32(P_VPU_PROT1_X_START_END,  data32);

    data32  = (y_end    << 16)  |
              (y_start  << 0);
    aml_write_reg32(P_VPU_PROT1_Y_START_END,  data32);

    data32  = (y_step   << 16)  |
              (y_len_m1 << 0);
    aml_write_reg32(P_VPU_PROT1_Y_LEN_STEP,   data32);

    data32  = (pat_start_ptr    << 4)   |
              (pat_end_ptr      << 0);
    aml_write_reg32(P_VPU_PROT1_RPT_LOOP,     data32);

    aml_write_reg32(P_VPU_PROT1_RPT_PAT,      pat_val);

    data32  = (cugt         << 20)  |
              (cid_mode     << 16)  |
              (cid_val      << 8)   |
              (canv_addr    << 0);
    aml_write_reg32(P_VPU_PROT1_DDR,          data32);

    data32  = (hold_lines       << 8)   |
              (little_endian    << 7)   |
              (conv_422to444    << 6)   |
              (bytes_per_pixel  << 4)   |
              (y_rev            << 3)   |
              (x_rev            << 2)   |
              (1                << 0);      // [1:0] req_en: 0=Idle; 1=Rotate mode; 2=FIFO mode.
    aml_write_reg32(P_VPU_PROT1_GEN_CNTL,     data32);

    data32  = (req_onoff_en << 31)  |
              (req_off_min  << 16)  |
              (req_on_max   << 0);
    aml_write_reg32(P_VPU_PROT1_REQ_ONOFF,    data32);
    aml_write_reg32(P_VPU_PROT1_CLK_GATE, 1); // Enable clock
    return 0;
}   

