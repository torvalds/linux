/*
drivers/video/display/transmitter/rk616_mipi_dsi.h
*/
#ifndef RK616_MIPI_DSI_H
#define RK616_MIPI_DSI_H

#define DWC_DSI_VERSION	0x3131302A


#define MIPI_DSI_PHY_OFFSET		0X0C00
#define MIPI_DSI_HOST_OFFSET	0x1000

#define RK_ADDR(A)    (MIPI_DSI_PHY_OFFSET + (A << 2))

//MIPI DSI HOST REGISTER
#define VERSION 		(MIPI_DSI_HOST_OFFSET + 0x00) 		// Version of the DSI host controller 0x3130312A
#define PWR_UP 			(MIPI_DSI_HOST_OFFSET + 0x04)		//R/W Core power up 0x0
#define CLKMGR_CFG 		(MIPI_DSI_HOST_OFFSET + 0x08)		//R/W Number of active data lanes 
#define DPI_CFG 		(MIPI_DSI_HOST_OFFSET + 0x0C)		//R/W DPI interface configuration 
//#define DBI_CFG 		(MIPI_DSI_HOST_OFFSET + 0x10)		//R/W DBI interface configuration 0x0
//#define DBI_CMDSIZE 	(MIPI_DSI_HOST_OFFSET + 0x14)		//R/W DBI command size configuration 0x0
#define PCKHDL_CFG 		(MIPI_DSI_HOST_OFFSET + 0x18)		//R/W Packet handler configuration 0x0
#define VID_MODE_CFG 	(MIPI_DSI_HOST_OFFSET + 0x1C)		//R/W Video mode Configuration 0x0
#define VID_PKT_CFG 	(MIPI_DSI_HOST_OFFSET + 0x20)		//R/W Video packet configuration 0x0
#define CMD_MODE_CFG 	(MIPI_DSI_HOST_OFFSET + 0x24)		//R/W Command mode configuration 0x0
#define TMR_LINE_CFG 	(MIPI_DSI_HOST_OFFSET + 0x28)		//R/W Line timer configuration 0x0
#define VTIMING_CFG 	(MIPI_DSI_HOST_OFFSET + 0x2C)		//R/W Vertical timing configuration 0x0
#define PHY_TMR_CFG 	(MIPI_DSI_HOST_OFFSET + 0x30)		//R/W D-PHY timing configuration 0x0
#define GEN_HDR 		(MIPI_DSI_HOST_OFFSET + 0x34)		//R/W Generic packet header configuration 0x0
#define GEN_PLD_DATA 	(MIPI_DSI_HOST_OFFSET + 0x38)		//R/W Generic payload data in/out 0x0
#define CMD_PKT_STATUS 	(MIPI_DSI_HOST_OFFSET + 0x3C)		//R Command packet status 0x1515
#define TO_CNT_CFG 		(MIPI_DSI_HOST_OFFSET + 0x40)		//R/W Timeout timers configuration 
#define ERROR_ST0 		(MIPI_DSI_HOST_OFFSET + 0x44)		//R Interrupt status register 0 
#define ERROR_ST1 		(MIPI_DSI_HOST_OFFSET + 0x48)		//R Interrupt status register 1 0x0
#define ERROR_MSK0 		(MIPI_DSI_HOST_OFFSET + 0x4C)		//R/W Masks the interrupt generation triggered 0x0
#define ERROR_MSK1 		(MIPI_DSI_HOST_OFFSET + 0x50)		//R/W Masks the interrupt generation triggered 0x0 
#define PHY_RSTZ 		(MIPI_DSI_HOST_OFFSET + 0x54)		//R/W D-PHY reset control 0x0
#define PHY_IF_CFG 		(MIPI_DSI_HOST_OFFSET + 0x58)		//R/W D-PHY interface configuration 0x0
#define PHY_IF_CTRL 	(MIPI_DSI_HOST_OFFSET + 0x5C)		//R/W D-PHY PPI interface control 0x0
#define PHY_STATUS 		(MIPI_DSI_HOST_OFFSET + 0x60)		//R D-PHY PPI status interface 0x0
#define PHY_TST_CTRL0 	(MIPI_DSI_HOST_OFFSET + 0x64)		//R/W D-PHY test interface control 0 0x1
#define PHY_TST_CTRL1 	(MIPI_DSI_HOST_OFFSET + 0x68)		//R/W D-PHY test interface control 1 0x0
//#define EDPI_CFG 		(MIPI_DSI_HOST_OFFSET + 0x6C)		//R/W eDPI interface configuration 0x0
#define LP_CMD_TIM 		(MIPI_DSI_HOST_OFFSET + 0x70)		//R/W Low-Power command timing 0x0 configuration 


//function bits definition    register addr | bits | offest
#define REG_ADDR(a)			(a << 16)
#define REG_BITS(a)			(a << 8)
#define BITS_OFFSET(a)		(a)

#define shutdownz 					(PWR_UP << 16 | 1 << 8 | 0 )
#define en18_loosely 				((DPI_CFG << 16) | (1 << 8) | (10))
#define colorm_active_low 			((DPI_CFG << 16) | (1 << 8) | (9))
#define shutd_active_low  			((DPI_CFG << 16) | (1 << 8) | (8))
#define hsync_active_low  			((DPI_CFG << 16) | (1 << 8) | (7))
#define vsync_active_low  			((DPI_CFG << 16) | (1 << 8) | (6))
#define dataen_active_low  			((DPI_CFG << 16) | (1 << 8) | (5))
#define dpi_color_coding 			((DPI_CFG << 16) | (3 << 8) | (2))
#define dpi_vid						((DPI_CFG << 16) | (1 << 8) | (0))

#define hline_time  				(TMR_LINE_CFG << 16 | 14 << 8 | 18 )
#define hbp_time 					(TMR_LINE_CFG << 16 | 9 << 8 | 9 )
#define hsa_time 					(TMR_LINE_CFG << 16 | 9 << 8 | 0 )

#define v_active_lines 				(VTIMING_CFG << 16 | 11 << 8 | 16 )
#define vfp_lines 					(VTIMING_CFG << 16 | 6 << 8 | 10 )
#define vbp_lines 					(VTIMING_CFG << 16 | 6 << 8 | 4 )
#define vsa_lines 					(VTIMING_CFG << 16 | 4 << 8 | 0 )


#define TO_CLK_DIVISION 			(REG_ADDR(CLKMGR_CFG) | REG_BITS(8) | BITS_OFFSET(8))
#define TX_ESC_CLK_DIVISION 		(REG_ADDR(CLKMGR_CFG) | REG_BITS(8) | BITS_OFFSET(0))

#define gen_vid_rx 					(REG_ADDR(PCKHDL_CFG) | REG_BITS(2) | BITS_OFFSET(5))
#define en_CRC_rx 					(REG_ADDR(PCKHDL_CFG) | REG_BITS(1) | BITS_OFFSET(4))
#define en_ECC_rx 					(REG_ADDR(PCKHDL_CFG) | REG_BITS(1) | BITS_OFFSET(3))
#define en_BTA 						(REG_ADDR(PCKHDL_CFG) | REG_BITS(1) | BITS_OFFSET(2))
#define en_EOTp_rx 					(REG_ADDR(PCKHDL_CFG) | REG_BITS(1) | BITS_OFFSET(1))
#define en_EOTp_tx 					(REG_ADDR(PCKHDL_CFG) | REG_BITS(1) | BITS_OFFSET(0))

#define lpcmden  					(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(12))
#define frame_BTA_ack 				(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(11))
#define en_null_pkt 				(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(10))
#define en_multi_pkt 				(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(9))
#define en_lp_hfp 					(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(8))
#define en_lp_hbp 					(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(7))
#define en_lp_vact 					(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(6))
#define en_lp_vfp 					(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(5))
#define en_lp_vbp 					(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(4))
#define en_lp_vsa 					(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(3))
#define vid_mode_type 				(REG_ADDR(VID_MODE_CFG) | REG_BITS(2) | BITS_OFFSET(1))
#define en_video_mode 				(REG_ADDR(VID_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(0))

#define null_pkt_size 				(REG_ADDR(VID_PKT_CFG) | REG_BITS(10) | BITS_OFFSET(21))
#define num_chunks 					(REG_ADDR(VID_PKT_CFG) | REG_BITS(10) | BITS_OFFSET(11))
#define vid_pkt_size 				(REG_ADDR(VID_PKT_CFG) | REG_BITS(11) | BITS_OFFSET(0))

#define en_tear_fx 					(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(14))
#define en_ack_rqst 				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(13))
#define dcs_lw_tx 					(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(12))
#define gen_lw_tx 					(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(11))
#define max_rd_pkt_size 			(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(10))
#define dcs_sr_0p_tx 				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(9))
#define dcs_sw_1p_tx 				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(8))
#define dcs_sw_0p_tx				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(7))
#define gen_sr_2p_tx				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(6))
#define gen_sr_1p_tx 				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(5))
#define gen_sr_0p_tx 				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(4))
#define gen_sw_2p_tx 				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(3))
#define gen_sw_1p_tx 				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(2))
#define gen_sw_0p_tx 				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(1))
#define en_cmd_mode 				(REG_ADDR(CMD_MODE_CFG) | REG_BITS(1) | BITS_OFFSET(0))

#define phy_hs2lp_time 				(REG_ADDR(PHY_TMR_CFG) | REG_BITS(8) | BITS_OFFSET(24))
#define phy_lp2hs_time 				(REG_ADDR(PHY_TMR_CFG) | REG_BITS(8) | BITS_OFFSET(16))
#define max_rd_time 				(REG_ADDR(PHY_TMR_CFG) | REG_BITS(15) | BITS_OFFSET(0))

#define lprx_to_cnt 				(REG_ADDR(TO_CNT_CFG) | REG_BITS(16) | BITS_OFFSET(16))
#define hstx_to_cnt 				(REG_ADDR(TO_CNT_CFG) | REG_BITS(16) | BITS_OFFSET(0))

#define phy_enableclk 				(REG_ADDR(PHY_RSTZ) | REG_BITS(1) | BITS_OFFSET(2))
//#define phy_rstz 					(REG_ADDR(PHY_RSTZ) | REG_BITS(1) | BITS_OFFSET(1))
//#define phy_shutdownz 				(REG_ADDR(PHY_RSTZ) | REG_BITS(1) | BITS_OFFSET(0))

#define phy_stop_wait_time 			(REG_ADDR(PHY_IF_CFG) | REG_BITS(8) | BITS_OFFSET(2))
#define n_lanes 					(REG_ADDR(PHY_IF_CFG) | REG_BITS(2) | BITS_OFFSET(0))


#define phy_tx_triggers 			(REG_ADDR(PHY_IF_CTRL) | REG_BITS(4) | BITS_OFFSET(5))
#define phy_txexitulpslan 			(REG_ADDR(PHY_IF_CTRL) | REG_BITS(1) | BITS_OFFSET(4))
#define phy_txrequlpslan 			(REG_ADDR(PHY_IF_CTRL) | REG_BITS(1) | BITS_OFFSET(3))
#define phy_txexitulpsclk 			(REG_ADDR(PHY_IF_CTRL) | REG_BITS(1) | BITS_OFFSET(2))
#define phy_txrequlpsclk 			(REG_ADDR(PHY_IF_CTRL) | REG_BITS(1) | BITS_OFFSET(1))
#define phy_txrequestclkhs 			(REG_ADDR(PHY_IF_CTRL) | REG_BITS(1) | BITS_OFFSET(0))


#define phy_testclk 				(REG_ADDR(PHY_TST_CTRL0) | REG_BITS(1) | BITS_OFFSET(1))
#define phy_testclr 				(REG_ADDR(PHY_TST_CTRL0) | REG_BITS(1) | BITS_OFFSET(0))

#define phy_testen  				(REG_ADDR(PHY_TST_CTRL1) | REG_BITS(1) | BITS_OFFSET(16))
#define phy_testdout 				(REG_ADDR(PHY_TST_CTRL1) | REG_BITS(8) | BITS_OFFSET(8))
#define phy_testdin 				(REG_ADDR(PHY_TST_CTRL1) | REG_BITS(8) | BITS_OFFSET(0))

#define outvact_lpcmd_time  		(REG_ADDR(LP_CMD_TIM) | REG_BITS(8) | BITS_OFFSET(8))
#define invact_lpcmd_time 			(REG_ADDR(LP_CMD_TIM) | REG_BITS(8) | BITS_OFFSET(0))

#define gen_rd_cmd_busy  			(REG_ADDR(CMD_PKT_STATUS) | REG_BITS(1) | BITS_OFFSET(6))
#define gen_pld_r_full 				(REG_ADDR(CMD_PKT_STATUS) | REG_BITS(1) | BITS_OFFSET(5))
#define gen_pld_r_empty 			(REG_ADDR(CMD_PKT_STATUS) | REG_BITS(1) | BITS_OFFSET(4))
#define gen_pld_w_full 				(REG_ADDR(CMD_PKT_STATUS) | REG_BITS(1) | BITS_OFFSET(3))     //800byte    write GEN_PLD_DATA
#define gen_pld_w_empty 			(REG_ADDR(CMD_PKT_STATUS) | REG_BITS(1) | BITS_OFFSET(2))
#define gen_cmd_full 				(REG_ADDR(CMD_PKT_STATUS) | REG_BITS(1) | BITS_OFFSET(1))     //20   write GEN_HDR
#define gen_cmd_empty 				(REG_ADDR(CMD_PKT_STATUS) | REG_BITS(1) | BITS_OFFSET(0))

#define phystopstateclklane 		(REG_ADDR(PHY_STATUS) | REG_BITS(1) | BITS_OFFSET(2))
#define phylock 					(REG_ADDR(PHY_STATUS) | REG_BITS(1) | BITS_OFFSET(0))


//MIPI DSI DPHY REGISTERS
#define DPHY_REGISTER0		(MIPI_DSI_PHY_OFFSET + 0X0000)
#define DPHY_REGISTER1		(MIPI_DSI_PHY_OFFSET + 0X0004)
#define DPHY_REGISTER3		(MIPI_DSI_PHY_OFFSET + 0X000C)
#define DPHY_REGISTER4		(MIPI_DSI_PHY_OFFSET + 0X0010)
#define DPHY_REGISTER20		(MIPI_DSI_PHY_OFFSET + 0X0080)

#define lane_en_ck 					(REG_ADDR(DPHY_REGISTER0) | REG_BITS(1) | BITS_OFFSET(6))
#define lane_en_3 					(REG_ADDR(DPHY_REGISTER0) | REG_BITS(1) | BITS_OFFSET(5))
#define lane_en_2 					(REG_ADDR(DPHY_REGISTER0) | REG_BITS(1) | BITS_OFFSET(4))
#define lane_en_1 					(REG_ADDR(DPHY_REGISTER0) | REG_BITS(1) | BITS_OFFSET(3))
#define lane_en_0 					(REG_ADDR(DPHY_REGISTER0) | REG_BITS(1) | BITS_OFFSET(2))


#define reg_da_syncrst 				(REG_ADDR(DPHY_REGISTER1) | REG_BITS(1) | BITS_OFFSET(2))
#define reg_da_ldopd 				(REG_ADDR(DPHY_REGISTER1) | REG_BITS(1) | BITS_OFFSET(1))
#define reg_da_pllpd 				(REG_ADDR(DPHY_REGISTER1) | REG_BITS(1) | BITS_OFFSET(0))


#define reg_fbdiv_8 				(REG_ADDR(DPHY_REGISTER3) | REG_BITS(1) | BITS_OFFSET(5))
#define reg_prediv 					(REG_ADDR(DPHY_REGISTER3) | REG_BITS(5) | BITS_OFFSET(0))
#define reg_fbdiv 					(REG_ADDR(DPHY_REGISTER4) | REG_BITS(8) | BITS_OFFSET(0))

#define reg_dig_rstn 				(REG_ADDR(DPHY_REGISTER20) | REG_BITS(1) | BITS_OFFSET(0))


#define DPHY_CLOCK_OFFSET		(MIPI_DSI_PHY_OFFSET + 0X0100)
#define DPHY_LANE0_OFFSET		(MIPI_DSI_PHY_OFFSET + 0X0180)
#define DPHY_LANE1_OFFSET		(MIPI_DSI_PHY_OFFSET + 0X0200)
#define DPHY_LANE2_OFFSET		(MIPI_DSI_PHY_OFFSET + 0X0280)
#define DPHY_LANE3_OFFSET		(MIPI_DSI_PHY_OFFSET + 0X0300)

#define reg_ths_settle			0x0000
#define reg_hs_tlpx				0x0014
#define reg_hs_ths_prepare		0x0018
#define reg_hs_the_zero			0x001c
#define reg_hs_ths_trail		0x0020
#define reg_hs_ths_exit			0x0024
#define reg_hs_tclk_post		0x0028
#define reserved				0x002c
#define reg_hs_twakup_h			0x0030
#define reg_hs_twakup_l			0x0034
#define reg_hs_tclk_pre			0x0038
#define reg_hs_tta_go			0x0040
#define reg_hs_tta_sure			0x0044
#define reg_hs_tta_wait			0x0048


//MISC REGISTERS
#define CRU_CRU_CLKSEL1_CON		(0x005c)
#define CRU_CFG_MISC_CON		(0x009c)

#define cfg_mipiclk_gaten 			(REG_ADDR(CRU_CRU_CLKSEL1_CON) | REG_BITS(1) | BITS_OFFSET(10))

#define mipi_int 					(REG_ADDR(CRU_CFG_MISC_CON) | REG_BITS(1) | BITS_OFFSET(19))
#define mipi_edpihalt 				(REG_ADDR(CRU_CFG_MISC_CON) | REG_BITS(1) | BITS_OFFSET(16))
#define pin_forcetxstopmode_3 		(REG_ADDR(CRU_CFG_MISC_CON) | REG_BITS(1) | BITS_OFFSET(11))
#define pin_forcetxstopmode_2 		(REG_ADDR(CRU_CFG_MISC_CON) | REG_BITS(1) | BITS_OFFSET(10))
#define pin_forcetxstopmode_1 		(REG_ADDR(CRU_CFG_MISC_CON) | REG_BITS(1) | BITS_OFFSET(9))
#define pin_forcetxstopmode_0 		(REG_ADDR(CRU_CFG_MISC_CON) | REG_BITS(1) | BITS_OFFSET(8))
#define pin_forcerxmode_0 			(REG_ADDR(CRU_CFG_MISC_CON) | REG_BITS(1) | BITS_OFFSET(7))
#define pin_turndisable_0 			(REG_ADDR(CRU_CFG_MISC_CON) | REG_BITS(1) | BITS_OFFSET(6))
#define dpicolom 					(REG_ADDR(CRU_CFG_MISC_CON) | REG_BITS(1) | BITS_OFFSET(2))
#define dpishutdn 					(REG_ADDR(CRU_CFG_MISC_CON) | REG_BITS(1) | BITS_OFFSET(1))



//global operation timing parameter
struct gotp_m {
	//time uint is ns
	u32 min;
	u32 value;
	u32 max;
};

//default time unit is ns 
//Unit Interval, equal to the duration of any HS state on the Clock Lane
struct gotp {
	u32 CLK_MISS;    			//min:no    max:60
	u32 CLK_POST;    			//min:60 ns + 52*UI    max:no
	u32 CLK_PRE;     			//min:8*UI    max:no
	u32 CLK_PREPARE;  			//min:38    max:95
	u32 CLK_SETTLE;    			//min:95    max:300
	u32 CLK_TERM_EN;    		//min:Time for Dn to reach VTERM-EN    max:38
	u32 CLK_TRAIL;    			//min:60    max:no
	u32 CLK_ZERO;    			//min:300 - CLK_PREPARE    max:no
	u32 D_TERM_EN;    			//min:Time for Dn to reach VTERM-EN    max:35 ns + 4*UI
	u32 EOT;    				//min:no    max:105 ns + n*12*UI
	u32 HS_EXIT;    			//min:100    max:no
	u32 HS_PREPARE;    			//min:40 ns + 4*UI     max:85 ns + 6*UI	
	u32 HS_ZERO;    			//min:145 ns + 10*UI - HS_PREPARE    max:no
	u32 HS_SETTLE;    			//min:85 ns + 6*UI     max:145 ns + 10*UI
	u32 HS_SKIP;    			//min:40    max:55 ns + 4*UI
	u32 HS_TRAIL;    			//min: max( n*8*UI, 60 ns + n*4*UI )    max:no
	u32 NIT;    				//min:100us    max:no
	u32 LPX;    				//min:50    max:no
	u32 TA_GET;    				//min:5*TLPX    
	u32 TA_GO;    				//min:4*TLPX    	
	u32 TA_SURE;    			//min:TLPX    max:2*TLPX
	u32 WAKEUP;    				//min:1ms    max:no
};


struct dsi_phy {
	u32 UI;
	u32 ref_clk;     	//input_clk
	u32 ddr_clk;		//data bit clk
	u32 txbyte_clk;		//1/8 of ddr_clk
	u32 sys_clk;		//
	u32 pclk;			//
	u32 txclkesc;
	
	u32 Tddr_clk;		//ps
	u32 Ttxbyte_clk;   	//ps
	u32 Tsys_clk;   	//ps
	u32 Tpclk;   		//ps
	u32 Ttxclkesc;		//ps
	
	u16 prediv;
	u16 fbdiv;
	u8 flag;
	struct gotp gotp;

};


struct dsi_host {
	u8 flag;
	u8 lane;
	u8 format;
	u8 video_mode;
	u32 clk;

};

struct dsi {

	u8 lcdc_id;
	u8 vid;
	struct dsi_phy phy;
	struct dsi_host host;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif

};



//config
#define MIPI_DSI_REGISTER_IO	0

#ifndef MHz
#define MHz   1000000
#endif

#endif /* end of RK616_MIPI_DSI_H */
