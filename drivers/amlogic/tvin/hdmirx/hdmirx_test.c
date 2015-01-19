/*
 * Amlogic M6TV
 * HDMI RX
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
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
//#include <linux/amports/canvas.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <mach/clock.h>
#include <mach/register.h>
#include <mach/power_gate.h>

#include <linux/amlogic/tvin/tvin.h>
/* Local include */
#include "hdmirx_drv.h"
#include "hdmi_rx_reg.h"

#define HDMIRX_DEV_ID_TOP   0
#define HDMIRX_DEV_ID_DWC   1
#define HDMIRX_DEV_ID_PHY   2



#define EDID_AUTO_CHECKSUM_ENABLE   1               // Checksum byte selection: 0=Use data stored in MEM; 1=Use checksum calculated by HW.
#define EDID_CLK_DIVIDE_M1          2               // EDID I2C clock = sysclk / (1+EDID_CLK_DIVIDE_M1).
#define EDID_AUTO_CEC_ENABLE        0
                                                    // 0=Analog PLL based ACR;
                                                    // 1=Digital ACR.
#define VSYNC_POLARITY      1                       // TX VSYNC polarity: 0=low active; 1=high active.
#define RX_8_CHANNEL        1        // 0=I2S 2-channel; 1=I2S 4 x 2-channel.

#define RX_INPUT_COLOR_FORMAT 0
#define PIXEL_REPEAT_HDMI 0
#define RX_INPUT_COLOR_DEPTH 0
#define RX_HSCALE_HALF 0

#define MANUAL_ACR_N        6272
#define MANUAL_ACR_CTS      ((RX_INPUT_COLOR_DEPTH==0)? 30000 : (RX_INPUT_COLOR_DEPTH==1)? 30000*5/4 : (RX_INPUT_COLOR_DEPTH==2)? 30000*3/2 : 30000*2)
#define EXPECT_ACR_N        4096
#define EXPECT_ACR_CTS      19582

#define EXPECT_MEAS_RESULT  145057                  // = T(audio_master_clk) * meas_clk_cycles / T(hdmi_audmeas_ref_clk); where meas_clk_cycles=4096; T(hdmi_audmeas_ref_clk)=5 ns.

#define HDMI_ARCTX_EN       0                       // Audio Return Channel (ARC) transmission block control:0=Disable; 1=Enable.
#define HDMI_ARCTX_MODE     0                       // ARC transmission mode: 0=Single-ended mode; 1=Common mode.

#define AUD_CLK_DELTA   2000

#define INTERLACE_MODE 1    


#define Wr_reg_bits(reg, val, start, len) \
  WRITE_MPEG_REG(reg, (READ_MPEG_REG(reg) & ~(((1L<<(len))-1)<<(start)))|((unsigned int)(val) << (start)))


#define P_HDMIRX_CTRL_PORT    0xc800e008  // TOP CTRL_PORT: 0xc800e008; DWC CTRL_PORT: 0xc800e018
#define HDMIRX_ADDR_PORT    (0xe000)  // TOP ADDR_PORT: 0xc800e000; DWC ADDR_PORT: 0xc800e010
#define HDMIRX_DATA_PORT    (0xe004)  // TOP DATA_PORT: 0xc800e004; DWC DATA_PORT: 0xc800e014
#define HDMIRX_CTRL_PORT    (0xe008)  // TOP CTRL_PORT: 0xc800e008; DWC CTRL_PORT: 0xc800e018


void hdmirx_wr_dwc_check(uint16_t addr, uint32_t data)
{
   uint32_t rd_back;
   hdmirx_wr_dwc(addr, data);
   rd_back = hdmirx_rd_dwc(addr);
   if(rd_back!=data){
        printk("%s error (%x,%x) read back is %x\n", __func__, addr, data, rd_back);
        while(1); 
   }
}

void hdmirx_wr_phy_check(uint16_t addr, uint32_t data)
{
   uint32_t rd_back;
   hdmirx_wr_phy(addr, data);
   rd_back = hdmirx_rd_phy(addr);
   if(rd_back!=data){
        printk("%s error (%x,%x) read back is %x\n", __func__, addr, data, rd_back);
        while(1); 
   }
}



void hdmirx_wr_top_check(uint16_t addr, uint32_t data)
{
   uint32_t rd_back;
   hdmirx_wr_top(addr, data);
   rd_back = hdmirx_rd_top(addr);
   if(rd_back!=data){
        printk("%s error (%x,%x) read back is %x\n", __func__, addr, data, rd_back);
        while(1); 
   }
}

#define HDCP_KEY_WR_TRIES		(5)
static void hdmi_rx_ctrl_hdcp_config( const struct hdmi_rx_ctrl_hdcp *hdcp)
{
	int error = 0;
	unsigned i = 0;
	unsigned k = 0;

	hdmirx_wr_bits_dwc( RA_HDCP_CTRL, HDCP_ENABLE, 0);
	//hdmirx_wr_bits_dwc(ctx, RA_HDCP_CTRL, KEY_DECRYPT_ENABLE, 1);
	hdmirx_wr_bits_dwc( RA_HDCP_CTRL, KEY_DECRYPT_ENABLE, 0);
	hdmirx_wr_dwc(RA_HDCP_SEED, hdcp->seed);
	for (i = 0; i < HDCP_KEYS_SIZE; i += 2) {
		for (k = 0; k < HDCP_KEY_WR_TRIES; k++) {
			if (hdmirx_rd_bits_dwc( RA_HDCP_STS, HDCP_KEY_WR_OK_STS) != 0) {
				break;
			}
		}
		if (k < HDCP_KEY_WR_TRIES) {
			hdmirx_wr_dwc(RA_HDCP_KEY1, hdcp->keys[i + 0]);
			hdmirx_wr_dwc(RA_HDCP_KEY0, hdcp->keys[i + 1]);
		} else {
			error = -EAGAIN;
			break;
		}
	}
	hdmirx_wr_dwc(RA_HDCP_BKSV1, hdcp->bksv[0]);
	hdmirx_wr_dwc(RA_HDCP_BKSV0, hdcp->bksv[1]);
	hdmirx_wr_bits_dwc( RA_HDCP_RPT_CTRL, REPEATER, hdcp->repeat? 1 : 0);
	hdmirx_wr_dwc(RA_HDCP_RPT_BSTATUS, 0);	/* nothing attached downstream */

  hdmirx_wr_bits_dwc( RA_HDCP_CTRL, HDCP_ENABLE, 1);
	
}

void hdmirx_rd_check_reg (unsigned char dev_id, unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    unsigned long rd_data = 0;
    if (dev_id == HDMIRX_DEV_ID_TOP) {
        rd_data = hdmirx_rd_top(addr);
    } else if (dev_id == HDMIRX_DEV_ID_DWC) {
        rd_data = hdmirx_rd_dwc(addr);
    } else if (dev_id == HDMIRX_DEV_ID_PHY) {
        rd_data = hdmirx_rd_phy(addr);
    }
    if ((rd_data | mask) != (exp_data | mask)) 
    {
        printk("Error: %s(%d) addr=0x%lx, rd_data=0x%lx, exp_data=0x%lx, mask=0x%lx\n",
            __func__, dev_id, addr, rd_data, exp_data, mask);
    }
    
} /* hdmirx_rd_check_reg */

void hdmirx_poll_dwc (unsigned long addr, unsigned long exp_data, unsigned long mask, unsigned long max_try)
{
    unsigned long rd_data;
    unsigned long cnt   = 0;
    unsigned char done  = 0;
    
    rd_data = hdmirx_rd_dwc(addr);
    while (((cnt < max_try) || (max_try == 0)) && (done != 1)) {
        if ((rd_data | mask) == (exp_data | mask)) {
            done = 1;
        } else {
            cnt ++;
            rd_data = hdmirx_rd_dwc(addr);
        }
    }
    if (done == 0) {
        printk("Error: hdmirx_poll_DWC access time-out!\n");
    }
} /* hdmirx_poll_DWC */

int hdmi_rx_ctrl_edid_update(void);

void hdmirx_hw_config_ori(int rx_port_sel)
{
    unsigned long   data32;

    printk("[TEST.C] Configure HDMIRX\n");
    Wr_reg_bits(HHI_GCLK_MPEG0, 1, 21, 1);  // Turn on clk_hdmirx_pclk, also = sysclk

    // Enable APB3 fail on error
    *((volatile unsigned long *) P_HDMIRX_CTRL_PORT)          |= (1 << 15);   // APB3 to HDMIRX-TOP err_en
    *((volatile unsigned long *) (P_HDMIRX_CTRL_PORT+0x10))   |= (1 << 15);   // APB3 to HDMIRX-DWC err_en
    //--------------------------------------------------------------------------
    // Enable HDMIRX interrupts:
    //--------------------------------------------------------------------------
    // [12]     meter_stable_chg_hdmi
    // [11]     vid_colour_depth_chg
    // [10]     vid_fmt_chg
    // [9:6]    hdmirx_5v_fall
    // [5:2]    hdmirx_5v_rise
    // [1]      edid_addr_intr
    // [0]      core_intr_rise: sub-interrupts will be configured later
    hdmirx_wr_top_check( HDMIRX_TOP_INTR_MASKN, 0x00001fff);
    
    //--------------------------------------------------------------------------
    // Step 1-13: RX_INITIAL_CONFIG
    //--------------------------------------------------------------------------

    // 1. DWC reset default to be active, until reg HDMIRX_TOP_SW_RESET[0] is set to 0.
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_SW_RESET, 0x1, 0x0);
    
    // 2. turn on clocks: md, cfg...

    data32  = 0;
    data32 |= 0 << 25;  // [26:25] HDMIRX mode detection clock mux select: osc_clk
    data32 |= 1 << 24;  // [24]    HDMIRX mode detection clock enable
    data32 |= 0 << 16;  // [22:16] HDMIRX mode detection clock divider
    data32 |= 3 << 9;   // [10: 9] HDMIRX config clock mux select: fclk_div5=400MHz
    data32 |= 1 << 8;   // [    8] HDMIRX config clock enable
    data32 |= 3 << 0;   // [ 6: 0] HDMIRX config clock divider: 400/4=100MHz
    WRITE_MPEG_REG(HHI_HDMIRX_CLK_CNTL,     data32);

    data32  = 0;
    data32 |= 2             << 25;  // [26:25] HDMIRX ACR ref clock mux select: fclk_div5
    data32 |= rx.ctrl.acr_mode      << 24;  // [24]    HDMIRX ACR ref clock enable
    data32 |= 0             << 16;  // [22:16] HDMIRX ACR ref clock divider
    data32 |= 2             << 9;   // [10: 9] HDMIRX audmeas clock mux select: fclk_div5
    data32 |= 1             << 8;   // [    8] HDMIRX audmeas clock enable
    data32 |= 1             << 0;   // [ 6: 0] HDMIRX audmeas clock divider: 400/2 = 200MHz
    WRITE_MPEG_REG(HHI_HDMIRX_AUD_CLK_CNTL, data32);

    data32  = 0;
    data32 |= 1 << 17;  // [17]     audfifo_rd_en
    data32 |= 1 << 16;  // [16]     pktfifo_rd_en
    data32 |= 1 << 2;   // [2]      hdmirx_cecclk_en
    data32 |= 0 << 1;   // [1]      bus_clk_inv
    data32 |= 0 << 0;   // [0]      hdmi_clk_inv
    hdmirx_wr_top_check( HDMIRX_TOP_CLK_CNTL, data32);    // DEFAULT: {32'h0}

    // 3. wait for TX PHY clock up
    
    // 4. wait for rx sense
    
    // 5. Release IP reset
    hdmirx_wr_top_check( HDMIRX_TOP_SW_RESET, 0x0);

#if 1
    mdelay(100);
#endif

    // 6. Enable functional modules
    data32  = 0;
    data32 |= 1 << 5;   // [5]      cec_enable
    data32 |= 1 << 4;   // [4]      aud_enable
    data32 |= 1 << 3;   // [3]      bus_enable
    data32 |= 1 << 2;   // [2]      hdmi_enable
    data32 |= 1 << 1;   // [1]      modet_enable
    data32 |= 1 << 0;   // [0]      cfg_enable
    hdmirx_wr_dwc_check( RA_DMI_DISABLE_IF, data32);    // DEFAULT: {31'd0, 1'b0}
    //WRITE_MPEG_REG(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 5 ) {}  // delay 5uS
            mdelay(1);

    // 7. Reset functional modules
    hdmirx_wr_dwc( RA_DMI_SW_RST,     0x0000007F);
    //WRITE_MPEG_REG(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 10 ) {} // delay 10uS
            mdelay(1);
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, RA_DMI_SW_RST,   0, 0);
    
    // 8. If defined, force manual N & CTS to speed up simulation

    data32  = 0;
    data32 |= 0         << 9;   // [9]      force_afif_status:1=Use cntl_audfifo_status_cfg as fifo status; 0=Use detected afif_status.
    data32 |= 1         << 8;   // [8]      afif_status_auto:1=Enable audio FIFO status auto-exit EMPTY/FULL, if FIFO level is back to LipSync; 0=Once enters EMPTY/FULL, never exits.
    data32 |= 1         << 6;   // [ 7: 6]  Audio FIFO nominal level :0=s_total/4;1=s_total/8;2=s_total/16;3=s_total/32.
    data32 |= 3         << 4;   // [ 5: 4]  Audio FIFO critical level:0=s_total/4;1=s_total/8;2=s_total/16;3=s_total/32.
    data32 |= 0         << 3;   // [3]      afif_status_clr:1=Clear audio FIFO status to IDLE.
    data32 |= rx.ctrl.acr_mode  << 2;   // [2]      dig_acr_en
    data32 |= 0         << 1;   // [1]      audmeas_clk_sel: 0=select aud_pll_clk; 1=select aud_acr_clk.
    data32 |= rx.ctrl.acr_mode  << 0;   // [0]      aud_clk_sel: 0=select aud_pll_clk; 1=select aud_acr_clk.
    hdmirx_wr_top_check( HDMIRX_TOP_ACR_CNTL_STAT, data32);

    hdmirx_wr_dwc_check( RA_AUDPLL_GEN_CTS, MANUAL_ACR_CTS);
    hdmirx_wr_dwc_check( RA_AUDPLL_GEN_N,   MANUAL_ACR_N);

    // Force N&CTS to start with, will switch to received values later on, for simulation speed up.
    data32  = 0;
    data32 |= 1 << 4;   // [4]      cts_n_ref: 0=used decoded; 1=use manual N&CTS.
    hdmirx_wr_dwc_check( RA_AUD_CLK_CTRL,   data32);

    data32  = 0;
    data32 |= 0 << 28;  // [28]     pll_lock_filter_byp
    data32 |= 0 << 24;  // [27:24]  pll_lock_toggle_div
    hdmirx_wr_dwc_check( RA_AUD_PLL_CTRL,   data32);    // DEFAULT: {1'b0, 3'd0, 4'd6, 4'd3, 4'd8, 1'b0, 1'b0, 1'b1, 1'b0, 12'd0}
    
    // 9. Set EDID data at RX
#if 1
    hdmi_rx_ctrl_edid_update();

    data32  = 0;
    data32 |= 0                         << 13;  // [   13]  checksum_init_mode
    data32 |= EDID_AUTO_CHECKSUM_ENABLE << 12;  // [   12]  auto_checksum_enable
    data32 |= EDID_AUTO_CEC_ENABLE      << 11;  // [   11]  auto_cec_enable
    data32 |= 0                         << 10;  // [   10]  scl_stretch_trigger_config
    data32 |= 0                         << 9;   // [    9]  force_scl_stretch_trigger
    data32 |= 1                         << 8;   // [    8]  scl_stretch_enable
    data32 |= EDID_CLK_DIVIDE_M1 << 0;   // [ 7: 0]  clk_divide_m1
    hdmirx_wr_top_check( HDMIRX_TOP_EDID_GEN_CNTL,  data32);

	if(hdcp_enable){
		hdmi_rx_ctrl_hdcp_config(&rx.hdcp);
	} else {
		hdmirx_wr_bits_dwc( RA_HDCP_CTRL, HDCP_ENABLE, 0);
	}

#else
    hdmirx_edid_setting(edid_extension_flag);

    data32  = 0;
    data32 |= 0                         << 13;  // [   13]  checksum_init_mode
    data32 |= edid_auto_checksum_enable << 12;  // [   12]  auto_checksum_enable
    data32 |= edid_auto_cec_enable      << 11;  // [   11]  auto_cec_enable
    data32 |= 0                         << 10;  // [   10]  scl_stretch_trigger_config
    data32 |= 0                         << 9;   // [    9]  force_scl_stretch_trigger
    data32 |= 1                         << 8;   // [    8]  scl_stretch_enable
    data32 |= edid_clk_divide_m1 << 0;   // [ 7: 0]  clk_divide_m1
    hdmirx_wr_top( HDMIRX_TOP_EDID_GEN_CNTL,  data32);
    
    if (edid_cec_id_addr != 0x00990098) {
        hdmirx_wr_top( HDMIRX_TOP_EDID_ADDR_CEC,  edid_cec_id_addr);
    }

    if (rx_port_sel == 0) {
        hdmirx_wr_top( HDMIRX_TOP_EDID_DATA_CEC_PORT01,  ((edid_cec_id_data&0xff)<<8) | (edid_cec_id_data>>8));
    } else if (rx_port_sel == 1) {
        hdmirx_wr_top( HDMIRX_TOP_EDID_DATA_CEC_PORT01,  (((edid_cec_id_data&0xff)<<8) | (edid_cec_id_data>>8))<<16);
    } else if (rx_port_sel == 2) {
        hdmirx_wr_top( HDMIRX_TOP_EDID_DATA_CEC_PORT23,  ((edid_cec_id_data&0xff)<<8) | (edid_cec_id_data>>8));
    } else { // rx_port_sel == 3
        hdmirx_wr_top( HDMIRX_TOP_EDID_DATA_CEC_PORT23,  (((edid_cec_id_data&0xff)<<8) | (edid_cec_id_data>>8))<<16);
    }
    
    // 10. HDCP
    if (hdcp_on) {
        data32  = 0;
        data32 |= 0                     << 14;  // [14]     hdcp_vid_de: Force DE=1.
        data32 |= 0                     << 10;  // [11:10]  hdcp_sel_avmute: 0=normal mode.
        data32 |= 0                     << 8;   // [9:8]    hdcp_ctl: 0=automatic.
        data32 |= 0                     << 6;   // [7:6]    hdcp_ri_rate: 0=Ri exchange once every 128 frames.
        data32 |= hdcp_key_decrypt_en   << 1;   // [1]      key_decrypt_enable
        data32 |= hdcp_on               << 0;   // [0]      hdcp_enable
        hdmirx_wr_dwc( RA_HDCP_CTRL,  data32);
    
        data32  = 0;
        data32 |= 1                     << 16;  // [17:16]  i2c_spike_suppr
        data32 |= 1                     << 13;  // [13]     hdmi_reserved. 0=No HDMI capabilities.
        data32 |= 1                     << 12;  // [12]     fast_i2c
        data32 |= 1                     << 9;   // [9]      one_dot_one
        data32 |= 1                     << 8;   // [8]      fast_reauth
        data32 |= 0x3a                  << 1;   // [7:1]    hdcp_ddc_addr
        hdmirx_wr_dwc( RA_HDCP_SETTINGS,  data32);    // DEFAAULT: {13'd0, 2'd1, 1'b1, 3'd0, 1'b1, 2'd0, 1'b1, 1'b1, 7'd58, 1'b0}

        hdmirx_key_setting(hdcp_key_decrypt_en);
    } /* if (hdcp_on) */
#endif
    // 11. RX configuration

    hdmirx_wr_dwc_check( RA_HDMI_CKM_EVLTM, 0x0016fff0);
    hdmirx_wr_dwc_check( RA_HDMI_CKM_F,     0xf98a0190);

    data32  = 0;
    data32 |= 80    << 18;  // [26:18]  afif_th_start
    data32 |= 8     << 9;   // [17:9]   afif_th_max
    data32 |= 8     << 0;   // [8:0]    afif_th_min
    hdmirx_wr_dwc_check( RA_AUD_FIFO_TH,    data32);

    data32  = 0;
    data32 |= 0     << 24;  // [25:24]  mr_vs_pol_adj_mode
    data32 |= 0     << 18;  // [18]     spike_filter_en
    data32 |= 0     << 13;  // [17:13]  dvi_mode_hyst
    data32 |= 0     << 8;   // [12:8]   hdmi_mode_hyst
    data32 |= 0     << 6;   // [7:6]    hdmi_mode: 0=automatic
    data32 |= 2     << 4;   // [5:4]    gb_det
    data32 |= 0     << 2;   // [3:2]    eess_oess
    data32 |= 1     << 0;   // [1:0]    sel_ctl01
    hdmirx_wr_dwc_check( RA_HDMI_MODE_RECOVER,  data32); // DEFAULT: {6'd0, 2'd0, 5'd0, 1'b0, 5'd8, 5'd8, 2'd0, 2'd0, 2'd0, 2'd0}

    data32  = 0;
    data32 |= 1     << 31;  // [31]     pfifo_store_filter_en
    data32 |= 1     << 26;  // [26]     pfifo_store_mpegs_if
    data32 |= 1     << 25;  // [25]     pfifo_store_aud_if
    data32 |= 1     << 24;  // [24]     pfifo_store_spd_if
    data32 |= 1     << 23;  // [23]     pfifo_store_avi_if
    data32 |= 1     << 22;  // [22]     pfifo_store_vs_if
    data32 |= 1     << 21;  // [21]     pfifo_store_gmtp
    data32 |= 1     << 20;  // [20]     pfifo_store_isrc2
    data32 |= 1     << 19;  // [19]     pfifo_store_isrc1
    data32 |= 1     << 18;  // [18]     pfifo_store_acp
    data32 |= 0     << 17;  // [17]     pfifo_store_gcp
    data32 |= 0     << 16;  // [16]     pfifo_store_acr
    data32 |= 0     << 14;  // [14]     gcpforce_clravmute
    data32 |= 0     << 13;  // [13]     gcpforce_setavmute
    data32 |= 0     << 12;  // [12]     gcp_avmute_allsps
    data32 |= 0     << 8;   // [8]      pd_fifo_fill_info_clr
    data32 |= 0     << 6;   // [6]      pd_fifo_skip
    data32 |= 0     << 5;   // [5]      pd_fifo_clr
    data32 |= 1     << 4;   // [4]      pd_fifo_we
    data32 |= 1     << 0;   // [0]      pdec_bch_en
    hdmirx_wr_dwc_check( RA_PDEC_CTRL,  data32); // DEFAULT: {23'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 3'd0, 1'b0}

    data32  = 0;
    data32 |= 1     << 6;   // [6]      auto_vmute
    data32 |= 0xf   << 2;   // [5:2]    auto_spflat_mute
    hdmirx_wr_dwc_check( RA_PDEC_ASP_CTRL,  data32); // DEFAULT: {25'd0, 1'b1, 4'd0, 2'd0}

    data32  = 0;
    data32 |= 1     << 16;  // [16]     afif_subpackets: 0=store all sp; 1=store only the ones' spX=1.
    data32 |= 0     << 0;   // [0]      afif_init
    hdmirx_wr_dwc_check( RA_AUD_FIFO_CTRL,  data32); // DEFAULT: {13'd0, 2'd0, 1'b1, 15'd0, 1'b0}

    data32  = 0;
    data32 |= 0     << 10;  // [10]     ws_disable
    data32 |= 0     << 9;   // [9]      sck_disable
    data32 |= 0     << 5;   // [8:5]    i2s_disable
    data32 |= 0     << 1;   // [4:1]    spdif_disable
    data32 |= 1     << 0;   // [0]      i2s_32_16 
    hdmirx_wr_dwc_check( RA_AUD_SAO_CTRL,   data32); // DEFAULT: {21'd0, 1'b1, 1'b1, 4'd15, 4'd15, 1'b1}

    // Manual de-repeat to speed up simulation
    data32  = 0;
    data32 |= PIXEL_REPEAT_HDMI << 1;   // [4:1]    man_vid_derepeat
    data32 |= 0                 << 0;   // [0]      auto_derepeat
    hdmirx_wr_dwc_check( RA_HDMI_RESMPL_CTRL,   data32); // DEFAULT: {27'd0, 4'd0, 1'b1}

#if 0
    // At the 1st frame, HDMIRX hasn't received AVI packet, to speed up receiving YUV422 video, force oavi_video_format=1, release forcing on receiving AVI packet.
    if (RX_INPUT_COLOR_FORMAT == 1) {
        /*stimulus_event(31, STIMULUS_HDMI_UTIL_VID_FORMAT    |
                           (1                       << 4)   |   // 0=Release force; 1=Force vid_fmt
                           (RX_INPUT_COLOR_FORMAT   << 0));     // Video format: 0=RGB444; 1=YCbCr422; 2=YCbCr444.
                           */
    }
#endif

    // At the 1st frame, HDMIRX hasn't received AVI packet, HDMIRX default video format to RGB, so manual/force something to speed up simulation:
    data32  = 0;
    data32 |= (((RX_INPUT_COLOR_DEPTH==3) ||
                (RX_HSCALE_HALF==1))? 1:0)  << 29;  // [29]     cntl_vid_clk_half: To make timing easier, this bit can be set to 1: if input is dn-sample by 1, or input is 3*16-bit.
    data32 |= 0                             << 28;  // [28]     cntl_vs_timing: 0=Detect VS rising; 1=Detect HS falling.
    data32 |= 0                             << 27;  // [27]     cntl_hs_timing: 0=Detect HS rising; 1=Detect HS falling.
    // For receiving YUV444 video, we manually map component data to speed up simulation, manual-mapping will be cancelled once AVI is received.
    if (RX_INPUT_COLOR_FORMAT == 2) {
        data32 |= 2                         << 24;  // [26:24]  vid_data_map. 2={vid1, vid0, vid2}->{vid2, vid1, vid0}
    } else {
        data32 |= 0                         << 24;  // [26:24]  vid_data_map. 0={vid2, vid1, vid0}->{vid2, vid1, vid0}
    }
    data32 |= RX_HSCALE_HALF                << 23;  // [23]     hscale_half: 1=Horizontally scale down by half
    data32 |= 0                             << 22;  // [22]     force_vid_rate: 1=Force video output sample rate
    data32 |= 0                             << 19;  // [21:19]  force_vid_rate_chroma_cfg : 0=Bypass, not rate change. Applicable only if force_vid_rate=1
    data32 |= 0                             << 16;  // [18:16]  force_vid_rate_luma_cfg   : 0=Bypass, not rate change. Applicable only if force_vid_rate=1
    data32 |= 0x7fff                        << 0;   // [14: 0]  hsizem1
    hdmirx_wr_top_check( HDMIRX_TOP_VID_CNTL,   data32);

    // To speed up simulation:
    // Force VS polarity until for the first 2 frames, because it takes one whole frame for HDMIRX to detect the correct VS polarity;
    // HS polarity can be detected just after one line, so it can be set to auto-detect from the start.
    data32  = 0;
    data32 |= VSYNC_POLARITY    << 3;   // [4:3]    vs_pol_adj_mode:0=invert input VS; 1=no invert; 2=auto convert to high active; 3=no invert.
    data32 |= 2                 << 1;   // [2:1]    hs_pol_adj_mode:0=invert input VS; 1=no invert; 2=auto convert to high active; 3=no invert.
    hdmirx_wr_dwc_check( RA_HDMI_SYNC_CTRL,     data32); // DEFAULT: {27'd0, 2'd0, 2'd0, 1'b0}

    data32  = 0;
    data32 |= 3     << 21;  // [22:21]  aport_shdw_ctrl
    data32 |= 2     << 19;  // [20:19]  auto_aclk_mute
    data32 |= 1     << 10;  // [16:10]  aud_mute_speed
    data32 |= 1     << 7;   // [7]      aud_avmute_en
    data32 |= 1     << 5;   // [6:5]    aud_mute_sel
    data32 |= 1     << 3;   // [4:3]    aud_mute_mode
    data32 |= 0     << 1;   // [2:1]    aud_ttone_fs_sel
    data32 |= 0     << 0;   // [0]      testtone_en 
    hdmirx_wr_dwc_check( RA_AUD_MUTE_CTRL,  data32); // DEFAULT: {9'd0, 2'd0, 2'd0, 2'd0, 7'd48, 2'd0, 1'b1, 2'd3, 2'd3, 2'd0, 1'b0}

    data32  = 0;
    data32 |= 0     << 4;   // [11:4]   audio_fmt_chg_thres
    data32 |= 0     << 1;   // [2:1]    audio_fmt
    data32 |= 0     << 0;   // [0]      audio_fmt_sel
    hdmirx_wr_dwc_check( RA_AUD_PAO_CTRL,   data32); // DEFAULT: {20'd0, 8'd176, 1'b0, 2'd0, 1'b0}

    data32  = 0;
    data32 |= (RX_8_CHANNEL? 0x7 :0x0)  << 8;   // [10:8]   ch_map[7:5]
    data32 |= 1                         << 7;   // [7]      ch_map_manual
    data32 |= (RX_8_CHANNEL? 0x1f:0x3)  << 2;   // [6:2]    ch_map[4:0]
    data32 |= 1                         << 0;   // [1:0]    aud_layout_ctrl:0/1=auto layout; 2=layout 0; 3=layout 1.
    hdmirx_wr_dwc_check( RA_AUD_CHEXTRA_CTRL,    data32); // DEFAULT: {24'd0, 1'b0, 5'd0, 2'd0}

    data32  = 0;
    data32 |= 0     << 8;   // [8]      fc_lfe_exchg: 1=swap channel 3 and 4
    hdmirx_wr_dwc_check( RA_PDEC_AIF_CTRL,  data32); // DEFAULT: {23'd0, 1'b0, 8'd0}

    data32  = 0;
    data32 |= 0     << 20;  // [20]     rg_block_off:1=Enable HS/VS/CTRL filtering during active video
    data32 |= 1     << 19;  // [19]     block_off:1=Enable HS/VS/CTRL passing during active video
    data32 |= 5     << 16;  // [18:16]  valid_mode
    data32 |= 0     << 12;  // [13:12]  ctrl_filt_sens
    data32 |= 3     << 10;  // [11:10]  vs_filt_sens
    data32 |= 0     << 8;   // [9:8]    hs_filt_sens
    data32 |= 2     << 6;   // [7:6]    de_measure_mode
    data32 |= 0     << 5;   // [5]      de_regen
    data32 |= 3     << 3;   // [4:3]    de_filter_sens 
    hdmirx_wr_dwc_check( RA_HDMI_ERRORA_PROTECT, data32); // DEFAULT: {11'd0, 1'b0, 1'b0, 3'd0, 2'd0, 2'd0, 2'd0, 2'd0, 2'd0, 1'b0, 2'd0, 3'd0}

    data32  = 0;
    data32 |= 0     << 8;   // [10:8]   hact_pix_ith
    data32 |= 0     << 5;   // [5]      hact_pix_src
    data32 |= 1     << 4;   // [4]      htot_pix_src
    hdmirx_wr_dwc_check( RA_MD_HCTRL1,  data32); // DEFAULT: {21'd0, 3'd1, 2'd0, 1'b0, 1'b1, 4'd0}

    data32  = 0;
    data32 |= 1     << 12;  // [14:12]  hs_clk_ith
    data32 |= 7     << 8;   // [10:8]   htot32_clk_ith
    data32 |= 1     << 5;   // [5]      vs_act_time
    data32 |= 3     << 3;   // [4:3]    hs_act_time
    data32 |= 0     << 0;   // [1:0]    h_start_pos
    hdmirx_wr_dwc_check( RA_MD_HCTRL2,  data32); // DEFAULT: {17'd0, 3'd2, 1'b0, 3'd1, 2'd0, 1'b0, 2'd0, 1'b0, 2'd2}

    data32  = 0;
    data32 |= 1                 << 4;   // [4]      v_offs_lin_mode
    data32 |= 1                 << 1;   // [1]      v_edge
    data32 |= INTERLACE_MODE    << 0;   // [0]      v_mode
    hdmirx_wr_dwc_check( RA_MD_VCTRL,   data32); // DEFAULT: {27'd0, 1'b0, 2'd0, 1'b1, 1'b0}

    data32  = 0;
    data32 |= 1 << 10;  // [11:10]  vofs_lin_ith
    data32 |= 3 << 8;   // [9:8]    vact_lin_ith 
    data32 |= 0 << 6;   // [7:6]    vtot_lin_ith
    data32 |= 7 << 3;   // [5:3]    vs_clk_ith
    data32 |= 2 << 0;   // [2:0]    vtot_clk_ith
    hdmirx_wr_dwc_check( RA_MD_VTH,     data32); // DEFAULT: {20'd0, 2'd2, 2'd0, 2'd0, 3'd2, 3'd2}

    data32  = 0;
    data32 |= 1 << 2;   // [2]      fafielddet_en
    data32 |= 0 << 0;   // [1:0]    field_pol_mode
    hdmirx_wr_dwc_check( RA_MD_IL_POL,  data32); // DEFAULT: {29'd0, 1'b0, 2'd0}

    data32  = 0;
    data32 |= 0 << 2;   // [4:2]    deltacts_irqtrig
    data32 |= 0 << 0;   // [1:0]    cts_n_meas_mode
    hdmirx_wr_dwc_check( RA_PDEC_ACRM_CTRL, data32); // DEFAULT: {27'd0, 3'd0, 2'd1}

    // 12. RX PHY GEN3 configuration

#if 0
    // Turn on interrupts that to do with PHY communication
    hdmirx_wr_dwc( RA_HDMI_ICLR,         0xffffffff);
    *curr_hdmi_ien_maskn    = hdmi_ien_maskn;
    hdmirx_wr_dwc( RA_HDMI_IEN_SET,      hdmi_ien_maskn);
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, RA_HDMI_ISTS,        0, 0);
#endif
    // PDDQ = 1'b1; PHY_RESET = 1'b1;
    data32  = 0;
    data32 |= 1             << 6;   // [6]      physvsretmodez
    data32 |= 1             << 4;   // [5:4]    cfgclkfreq
    data32 |= rx_port_sel   << 2;   // [3:2]    portselect
    data32 |= 1             << 1;   // [1]      phypddq
    data32 |= 1             << 0;   // [0]      phyreset
    hdmirx_wr_dwc_check( RA_SNPS_PHYG3_CTRL,    data32); // DEFAULT: {27'd0, 3'd0, 2'd1}
    //WRITE_MPEG_REG(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 1 ) {} // delay 1uS
        mdelay(1);

    // PDDQ = 1'b1; PHY_RESET = 1'b0;
    data32  = 0;
    data32 |= 1             << 6;   // [6]      physvsretmodez
    data32 |= 1             << 4;   // [5:4]    cfgclkfreq
    data32 |= rx_port_sel   << 2;   // [3:2]    portselect
    data32 |= 1             << 1;   // [1]      phypddq
    data32 |= 0             << 0;   // [0]      phyreset
    hdmirx_wr_dwc_check( RA_SNPS_PHYG3_CTRL,    data32); // DEFAULT: {27'd0, 3'd0, 2'd1}

    // Configuring I2C to work in fastmode
    hdmirx_wr_dwc_check( RA_I2CM_PHYG3_MODE,    0x1);

    // Write PHY register 0x02 -> { 6'b001000, 1'b1, timebase_ovr[8:0]};
    //                                              - timebase_ovr[8:0] = Fcfg_clk(MHz) x 4;
    data32  = 0;
    data32 |= 8         << 10;  // [15:10]  lock_thres
    data32 |= 1         << 9;   // [9]      timebase_ovr_en
    data32 |= (25 * 4)  << 0;   // [8:0]    timebase_ovr = F_cfgclk(MHz) * 4
    hdmirx_wr_phy_check( 0x02, data32);

    //------------------------------------------------------------------------------------------
    // Write PHY register 0x03 -> {9'b000000100, color_depth[1:0], 5'b0000};
    //                                              - color_depth = 00 ->  8bits;
    //                                              - color_depth = 01 -> 10bits;
    //                                              - color_depth = 10 -> 12bits;
    //                                              - color_depth = 11 -> 16bits.
    //------------------------------------------------------------------------------------------
    data32  = 0;
    data32 |= 0                     << 15;  // [15]     mpll_short_power_up
    data32 |= 0                     << 13;  // [14:13]  mpll_mult
    data32 |= 0                     << 12;  // [12]     dis_off_lp
    data32 |= 0                     << 11;  // [11]     fast_switching
    data32 |= 0                     << 10;  // [10]     bypass_afe
    data32 |= 1                     << 9;   // [9]      fsm_enhancement
    data32 |= 0                     << 8;   // [8]      low_freq_eq
    data32 |= 0                     << 7;   // [7]      bypass_aligner
    data32 |= RX_INPUT_COLOR_DEPTH  << 5;   // [6:5]    color_depth: 0=8-bit; 1=10-bit; 2=12-bit; 3=16-bit.
    data32 |= 0                     << 3;   // [4:3]    sel_tmdsclk: 0=Use chan0 clk; 1=Use chan1 clk; 2=Use chan2 clk; 3=Rsvd.
    data32 |= 0                     << 2;   // [2]      port_select_ovr_en
    data32 |= 0                     << 0;   // [1:0]    port_select_ovr
    hdmirx_wr_phy(0x03, data32);

    // PDDQ = 1'b0; PHY_RESET = 1'b0;
    data32  = 0;
    data32 |= 1             << 6;   // [6]      physvsretmodez
    data32 |= 1             << 4;   // [5:4]    cfgclkfreq
    data32 |= rx_port_sel   << 2;   // [3:2]    portselect
    data32 |= 0             << 1;   // [1]      phypddq
    data32 |= 0             << 0;   // [0]      phyreset
    hdmirx_wr_dwc_check( RA_SNPS_PHYG3_CTRL,    data32); // DEFAULT: {27'd0, 3'd0, 2'd1}

    data32  = 0;
    data32 |= 0                     << 15;  // [15]     mpll_short_power_up
    data32 |= 0                     << 13;  // [14:13]  mpll_mult
    data32 |= 0                     << 12;  // [12]     dis_off_lp
    data32 |= 0                     << 11;  // [11]     fast_switching
    data32 |= 0                     << 10;  // [10]     bypass_afe
    data32 |= 1                     << 9;   // [9]      fsm_enhancement
    data32 |= 0                     << 8;   // [8]      low_freq_eq
    data32 |= 0                     << 7;   // [7]      bypass_aligner
    data32 |= RX_INPUT_COLOR_DEPTH  << 5;   // [6:5]    color_depth: 0=8-bit; 1=10-bit; 2=12-bit; 3=16-bit.
    data32 |= 0                     << 3;   // [4:3]    sel_tmdsclk: 0=Use chan0 clk; 1=Use chan1 clk; 2=Use chan2 clk; 3=Rsvd.
    data32 |= 0                     << 2;   // [2]      port_select_ovr_en
    data32 |= 0                     << 0;   // [1:0]    port_select_ovr
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_PHY, 0x03, data32, 0);

    // 13.  HDMI RX Ready! - Assert HPD
    printk("[TEST.C] HDMI RX Ready! - Assert HPD\n");

    hdmirx_wr_top_check( HDMIRX_TOP_PORT_SEL,   (1<<rx_port_sel));

    data32  = 0;
    data32 |= 1                 << 5;   // [    5]  invert_hpd
    data32 |= 1                 << 4;   // [    4]  force_hpd: default=1
    data32 |= (1<<rx_port_sel)  << 0;   // [ 3: 0]  hpd_config
    hdmirx_wr_top( HDMIRX_TOP_HPD_PWR5V,  data32);

    // Configure external video data generator and analyzer
    /*
    start_video_gen_ana(vic,                // Video format identification code
                        pixel_repeat_hdmi,
                        interlace_mode,     // 0=Progressive; 1=Interlace.
                        front_porch,        // Number of pixels from DE Low to HSYNC high
                        back_porch,         // Number of pixels from HSYNC low to DE high
                        hsync_pixels,       // Number of pixels of HSYNC pulse
                        hsync_polarity,     // TX HSYNC polarity: 0=low active; 1=high active.
                        sof_lines,          // HSYNC count between VSYNC de-assertion and first line of active video
                        eof_lines,          // HSYNC count between last line of active video and start of VSYNC
                        vsync_lines,        // HSYNC count of VSYNC assertion
                        vsync_polarity,     // TX VSYNC polarity: 0=low active; 1=high active.
                        total_pixels,       // Number of total pixels per line
                        total_lines);       // Number of total lines per frame
    */
    // 14.  RX_FINAL_CONFIG
    
    // RX PHY PLL configuration
    //get config for CMU
    /*stimulus_event(31, STIMULUS_HDMI_UTIL_CALC_PLL_CONFIG   |
                       (0   << 4)                           |   // mdclk freq: 0=24MHz; 1=25MHz; 2=27MHz.
                       (1   << 0));                             // tmds_clk_freq: 0=25MHz; 1=27MHz; 2=54MHz; 3=74.25MHz; 4=148.5MHz; 5=27*5/4MHz.
                       */
//        //margin of +/-0.78% for clock drift
//        clockrate_max[15:0] = (expected_clockrate[15:0]+expected_clockrate[15:7]);
//        clockrate_min[15:0] = (expected_clockrate[15:0]-expected_clockrate[15:7]);

    data32  = 0;
    data32 |= 1     << 20;  // [21:20]  lock_hyst
    data32 |= 0     << 16;  // [18:16]  clk_hyst
    data32 |= 2490  << 4;   // [15:4]   eval_time
    hdmirx_wr_dwc_check( RA_HDMI_CKM_EVLTM, data32);    // DEFAULT: {10'd0, 2'd1, 1'b0, 3'd0, 12'd4095, 3'd0, 1'b0}

    data32  = 0;
    data32 |= 3533  << 16;  // [31:16]  maxfreq
    data32 |= 3479  << 0;   // [15:0]   minfreq
    hdmirx_wr_dwc_check( RA_HDMI_CKM_F, data32);    // DEFAULT: {16'd63882, 16'd9009}

    // RX PHY PLL lock wait
    printk("[TEST.C] WAITING FOR TMDSVALID-------------------\n");
    //while (! (*hdmi_pll_lock)) {
    while(1){
        if( hdmirx_rd_dwc( RA_HDMI_PLL_LCK_STS) & 0x1)
            break;
        mdelay(1);
        //WRITE_MPEG_REG(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 10 ) {} // delay 10uS
    }
    hdmirx_poll_dwc( RA_HDMI_CKM_RESULT, 1<<16, ~(1<<16), 0);

    // 15. Waiting for AUDIO PLL to lock before performing RX synchronous resets!
    //hdmirx_poll_reg(HDMIRX_DEV_ID_DWC, RA_AUD_PLL_CTRL, 1<<31, ~(1<<31));

    // 16. RX Reset

    data32  = 0;
    data32 |= 0 << 5;   // [5]      cec_enable
    data32 |= 0 << 4;   // [4]      aud_enable
    data32 |= 0 << 3;   // [3]      bus_enable
    data32 |= 0 << 2;   // [2]      hdmi_enable
    data32 |= 0 << 1;   // [1]      modet_enable
    data32 |= 1 << 0;   // [0]      cfg_enable
    hdmirx_wr_dwc_check( RA_DMI_DISABLE_IF, data32);    // DEFAULT: {31'd0, 1'b0}

    //WRITE_MPEG_REG(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 1 ) {} // delay 1uS
        mdelay(1);
#if 0
    //--------------------------------------------------------------------------
    // Enable HDMIRX-DWC interrupts:
    //--------------------------------------------------------------------------
    
    hdmirx_wr_dwc( RA_PDEC_ICLR,         0xffffffff);
    hdmirx_wr_dwc( RA_AUD_CLK_ICLR,      0xffffffff);
    hdmirx_wr_dwc( RA_AUD_FIFO_ICLR,     0xffffffff);
    hdmirx_wr_dwc( RA_MD_ICLR,           0xffffffff);
    //hdmirx_wr_dwc( RA_HDMI_ICLR,         0xffffffff);

    *curr_pdec_ien_maskn     = pdec_ien_maskn;
    *curr_aud_clk_ien_maskn  = aud_clk_ien_maskn;
    *curr_aud_fifo_ien_maskn = aud_fifo_ien_maskn;
    *curr_md_ien_maskn       = md_ien_maskn;

    hdmirx_wr_dwc( RA_PDEC_IEN_SET,      pdec_ien_maskn);
    hdmirx_wr_dwc( RA_AUD_CLK_IEN_SET,   aud_clk_ien_maskn);
    hdmirx_wr_dwc( RA_AUD_FIFO_IEN_SET,  aud_fifo_ien_maskn);
    hdmirx_wr_dwc( RA_MD_IEN_SET,        md_ien_maskn);
    //hdmirx_wr_dwc( RA_HDMI_IEN_SET,      hdmi_ien_maskn);

    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, RA_PDEC_ISTS,        0, 0);
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, RA_AUD_CLK_ISTS,     0, 0);
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, RA_AUD_FIFO_ISTS,    0, 0);
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, RA_MD_ISTS,          0, 0);
    //hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, RA_HDMI_ISTS,        0, 0);
#endif    
    //--------------------------------------------------------------------------
    // Bring up RX
    //--------------------------------------------------------------------------
    data32  = 0;
    data32 |= 1 << 5;   // [5]      cec_enable
    data32 |= 1 << 4;   // [4]      aud_enable
    data32 |= 1 << 3;   // [3]      bus_enable
    data32 |= 1 << 2;   // [2]      hdmi_enable
    data32 |= 1 << 1;   // [1]      modet_enable
    data32 |= 1 << 0;   // [0]      cfg_enable
    hdmirx_wr_dwc_check( RA_DMI_DISABLE_IF, data32);    // DEFAULT: {31'd0, 1'b0}

    //WRITE_MPEG_REG(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 10 ) {} // delay 10uS
        mdelay(1);

    // To speed up simulation, reset the video generator after HDMIRX-PHY is locked,
    // so that HDMIRX-DWC doesn't have wait for a whole frame before seeing the 1st Vsync.
    //stimulus_event(31, STIMULUS_HDMI_UTIL_VGEN_RESET        | 1);
    //stimulus_event(31, STIMULUS_HDMI_UTIL_VGEN_RESET        | 0);

    // Enable HDMI_ARCTX if needed
    if (HDMI_ARCTX_EN) {
        data32  = 0;
        data32 |= HDMI_ARCTX_MODE   << 1;   // [1]      arctx_mode
        data32 |= 0                 << 0;   // [0]      arctx_en
        hdmirx_wr_top_check( HDMIRX_TOP_ARCTX_CNTL, data32);
        
        data32  = 0;
        data32 |= HDMI_ARCTX_MODE   << 1;   // [1]      arctx_mode
        data32 |= HDMI_ARCTX_EN     << 0;   // [0]      arctx_en
        hdmirx_wr_top_check( HDMIRX_TOP_ARCTX_CNTL, data32);
    }
//        register_read(`RX_HDMI_ERD_STS,supportreg,"VERBOSE_MODE");
//        check_vector("Acc valid indication",supportreg,32'd0,32,error_tmp,"NOPRINT"); errorsum(error,error_tmp,error);
//        wait_for(5000,"VERBOSE_MODE");
//        register_read(`RX_HDMI_ERD_STS,supportreg,"VERBOSE_MODE");
//        check_vector("Acc valid indication",supportreg,32'd7,32,error_tmp,"NOPRINT"); errorsum(error,error_tmp,error);
//
//        wait_for(5000,"VERBOSE_MODE");
//
//        display_msg( "END OF HDMI RX CONFIGURATION", "DEBUG_MODE");
//
//      register_read(  `RX_PDEC_STS  ,  supportreg, "VERBOSE_MODE");
//      register_read(  `RX_MD_IL_SKEW  ,  supportreg, "NO_PRINT");
//      {phase, skew} = supportreg[3:0];
//      if (phase) begin
//        display_msg("ERROR - PHASE should be 0! Phase 1 detected","VERBOSE_MODE");
//        errorsum(error,1,error);
//      end
//
//      display_msg("############################### VS vs HS Skew Results ##################################", "VERBOSE_MODE");
//      if ((skew != 0) && (skew != 3) && (skew != 4) && (skew != 7)) begin
//        $sformat(supportstring,"   ERROR - Current frame skew %d/8 of a line width (phase %d)                   ", (skew+1), phase);
//        errorsum(error,1,error);
//      end
//      else $sformat(supportstring,"    Current frame skew %d/8 of a line width (phase %d)                           ", (skew+1), phase);
//      display_msg(supportstring, "VERBOSE_MODE");
//      display_msg("########################################################################################", "VERBOSE_MODE");
//
//          register_read(  `RX_MD_VSC      , { supportreg[15:0], vs_clk_temp  }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_HT0      , { htot32_clk      , hs_clk       }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_HT1      , { htot_pix        , hofs_pix     }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_HACT_PX  , { supportreg[15:0], hact_pix     }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_VSC      , { supportreg[15:0], vs_clk_temp  }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_VTC      , { vtot_clk                       }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_VOL      , { supportreg[15:0], vofs_lin     }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_VAL      , { supportreg[15:0], vact_lin     }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_VTL      , { supportreg[15:0], vtot_lin     }  , "VERBOSE_MODE");
//      register_read(  `RX_AUD_FIFO_STS    , supportreg  , "VERBOSE_MODE");
 if(rx.ctrl.acr_mode == 0){
	WRITE_MPEG_REG(HHI_AUDCLK_PLL_CNTL,  0x60010000); 
	WRITE_MPEG_REG(HHI_AUDCLK_PLL_CNTL2, 0x814d3928);
	WRITE_MPEG_REG(HHI_AUDCLK_PLL_CNTL3, 0x6b425012);
	WRITE_MPEG_REG(HHI_AUDCLK_PLL_CNTL4, 0x101);
	WRITE_MPEG_REG(HHI_AUDCLK_PLL_CNTL5, 0x8550d20);
	if(rx.aud_info.audio_recovery_clock > (96000 + AUD_CLK_DELTA)){
		if(rx.ctrl.tmds_clk2 <= 36000000) {
			printk("tmds_clk2 <= 36000000\n");
			WRITE_MPEG_REG(HHI_AUDCLK_PLL_CNTL6, 0x55013000);
		} else if (rx.ctrl.tmds_clk2 <= 53000000) {
			printk("tmds_clk2 <= 53000000\n");
			WRITE_MPEG_REG(HHI_AUDCLK_PLL_CNTL6, 0x55053000);
		} else {
			printk("tmds_clk2 > 53000000\n");
			WRITE_MPEG_REG(HHI_AUDCLK_PLL_CNTL6, 0x55153000);
		}
	} else {
		printk("audio_recovery_clock < 98000\n");
		WRITE_MPEG_REG(HHI_AUDCLK_PLL_CNTL6, 0x55153000);
	}
	WRITE_MPEG_REG(HHI_AUDCLK_PLL_CNTL, 0x00010000);  //reset
}

}

