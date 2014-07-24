/*
drivers/video/rockchip/transmitter/rk32_mipi_dsi.h
*/

#ifndef RK32_MIPI_DSI_H
#define RK32_MIPI_DSI_H

#ifdef CONFIG_RK_3288_DSI_UBOOT
#include <asm/arch/rkplat.h>
#define RK_GRF_VIRT			RKIO_GRF_PHYS
#define RK3288_CRU_PHYS			RKIO_CRU_PHYS

#define RK3288_GRF_SOC_CON6             GRF_SOC_CON6
#define RK3288_GRF_SOC_CON14            GRF_SOC_CON14
#else
#include <linux/rockchip/grf.h>
#endif
#define MIPI_DSI_HOST_OFFSET	0x1000

//function bits definition    register addr | bits | offest
#define REG_ADDR(a)			((a) << 16)
#define REG_BITS(a)			((a) << 8)
#define BITS_OFFSET(a)		(a)
#define DSI_HOST_BITS(addr, bits, bit_offset)  (REG_ADDR((addr)+MIPI_DSI_HOST_OFFSET) \
		| REG_BITS(bits) | BITS_OFFSET(bit_offset))  

//DWC_DSI_VERSION_0x3133302A
#define VERSION 					DSI_HOST_BITS(0x000, 32, 0)
#define shutdownz 					DSI_HOST_BITS(0x004, 1, 0)
#define TO_CLK_DIVISION 			DSI_HOST_BITS(0x008, 8, 8)
#define TX_ESC_CLK_DIVISION 		DSI_HOST_BITS(0x008, 8, 0)
#define dpi_vcid					DSI_HOST_BITS(0x00c, 2, 0)
#define en18_loosely 				DSI_HOST_BITS(0x010, 1, 8)
#define dpi_color_coding 			DSI_HOST_BITS(0x010, 4, 0)        //need modify in code
#define colorm_active_low 			DSI_HOST_BITS(0x014, 1, 4)
#define shutd_active_low  			DSI_HOST_BITS(0x014, 1, 3)
#define hsync_active_low  			DSI_HOST_BITS(0x014, 1, 2)
#define vsync_active_low  			DSI_HOST_BITS(0x014, 1, 1)
#define dataen_active_low  			DSI_HOST_BITS(0x014, 1, 0)
#define outvact_lpcmd_time  		DSI_HOST_BITS(0x018, 8, 16)   //attence
#define invact_lpcmd_time 			DSI_HOST_BITS(0x018, 8, 0)
//#define dbi_vcid					DSI_HOST_BITS(0x01c, 2, 0)
#define crc_rx_en 					DSI_HOST_BITS(0x02c, 1, 4)
#define ecc_rx_en 					DSI_HOST_BITS(0x02c, 1, 3)
#define bta_en 						DSI_HOST_BITS(0x02c, 1, 2)
#define eotp_rx_en 					DSI_HOST_BITS(0x02c, 1, 1)
#define eotp_tx_en 					DSI_HOST_BITS(0x02c, 1, 0)
#define gen_vid_rx 					DSI_HOST_BITS(0x030, 2, 0) 
#define cmd_video_mode 				DSI_HOST_BITS(0x034, 1, 0)
#define vpg_orientation             DSI_HOST_BITS(0x038, 1, 24) 
#define vpg_mode                    DSI_HOST_BITS(0x038, 1, 20) 
#define vpg_en                      DSI_HOST_BITS(0x038, 1, 16) 
#define lp_cmd_en  					DSI_HOST_BITS(0x038, 1, 15)
#define frame_bta_ack_en 			DSI_HOST_BITS(0x038, 1, 14)
#define lp_hfp_en 					DSI_HOST_BITS(0x038, 1, 13)
#define lp_hbp_en 					DSI_HOST_BITS(0x038, 1, 12)
#define lp_vact_en 					DSI_HOST_BITS(0x038, 1, 11)
#define lp_vfp_en 					DSI_HOST_BITS(0x038, 1, 10)
#define lp_vbp_en 					DSI_HOST_BITS(0x038, 1, 9)
#define lp_vsa_en 					DSI_HOST_BITS(0x038, 1, 8)
#define vid_mode_type 				DSI_HOST_BITS(0x038, 2, 0)
#define vid_pkt_size 				DSI_HOST_BITS(0x03c, 14, 0)
#define num_chunks 					DSI_HOST_BITS(0x040, 13, 0)
#define null_pkt_size 				DSI_HOST_BITS(0x044, 13, 0)
#define vid_hsa_time 				DSI_HOST_BITS(0x048, 12, 0)
#define vid_hbp_time 				DSI_HOST_BITS(0x04c, 12, 0)
#define vid_hline_time  			DSI_HOST_BITS(0x050, 15, 0)
#define vid_vsa_lines 				DSI_HOST_BITS(0x054, 10, 0)
#define vid_vbp_lines 				DSI_HOST_BITS(0x058, 10, 0)
#define vid_vfp_lines 				DSI_HOST_BITS(0x05c, 10, 0)
#define vid_active_lines 			DSI_HOST_BITS(0x060, 14, 0)
#define max_rd_pkt_size 			DSI_HOST_BITS(0x068, 1, 24)
#define dcs_lw_tx 					DSI_HOST_BITS(0x068, 1, 19)
#define dcs_sr_0p_tx 				DSI_HOST_BITS(0x068, 1, 18)
#define dcs_sw_1p_tx 				DSI_HOST_BITS(0x068, 1, 17)
#define dcs_sw_0p_tx				DSI_HOST_BITS(0x068, 1, 16)
#define gen_lw_tx 					DSI_HOST_BITS(0x068, 1, 14)
#define gen_sr_2p_tx				DSI_HOST_BITS(0x068, 1, 13)
#define gen_sr_1p_tx 				DSI_HOST_BITS(0x068, 1, 12)
#define gen_sr_0p_tx 				DSI_HOST_BITS(0x068, 1, 11)
#define gen_sw_2p_tx 				DSI_HOST_BITS(0x068, 1, 10)
#define gen_sw_1p_tx 				DSI_HOST_BITS(0x068, 1, 9)
#define gen_sw_0p_tx 				DSI_HOST_BITS(0x068, 1, 8)
#define ack_rqst_en 				DSI_HOST_BITS(0x068, 1, 1)
#define tear_fx_en 					DSI_HOST_BITS(0x068, 1, 0)
#define GEN_HDR						DSI_HOST_BITS(0x06c, 32, 0)    
#define GEN_PLD_DATA				DSI_HOST_BITS(0x070, 32, 0)	  //need modify
#define gen_rd_cmd_busy  			DSI_HOST_BITS(0x074, 1, 6)
#define gen_pld_r_full 				DSI_HOST_BITS(0x074, 1, 5)
#define gen_pld_r_empty 			DSI_HOST_BITS(0x074, 1, 4)
#define gen_pld_w_full 				DSI_HOST_BITS(0x074, 1, 3)     //800byte    write GEN_PLD_DATA
#define gen_pld_w_empty				DSI_HOST_BITS(0x074, 1, 2)
#define gen_cmd_full 				DSI_HOST_BITS(0x074, 1, 1)     //20   write GEN_HDR
#define gen_cmd_empty 				DSI_HOST_BITS(0x074, 1, 0)
#define hstx_to_cnt 				DSI_HOST_BITS(0x078, 16, 16)   //need modify
#define lprx_to_cnt 				DSI_HOST_BITS(0x078, 16, 0)
#define hs_rd_to_cnt 				DSI_HOST_BITS(0x07c, 16, 0)     //new(read)
#define lp_rd_to_cnt 				DSI_HOST_BITS(0x080, 16, 0)		//new(read)
#define presp_to_mode 				DSI_HOST_BITS(0x084, 1, 24)		//new
#define hs_wr_to_cnt 				DSI_HOST_BITS(0x084, 16, 0)		//new
#define lp_wr_to_cnt 				DSI_HOST_BITS(0x088, 16, 0)		//new
#define bta_to_cnt 					DSI_HOST_BITS(0x08c, 16, 0)		//new
//#define send_3d_cfg 				DSI_HOST_BITS(0x090, 1, 16)		//new
//#define right_first 				DSI_HOST_BITS(0x090, 1, 5)		//new
//#define second_vsync 				DSI_HOST_BITS(0x090, 1, 4)		//new
//#define format_3d 					DSI_HOST_BITS(0x090, 2, 2)		//new
//#define mode_3d		 				DSI_HOST_BITS(0x090, 2, 0)		//new
#define auto_clklane_ctrl 			DSI_HOST_BITS(0x094, 1, 1)		//new
#define phy_txrequestclkhs 			DSI_HOST_BITS(0x094, 1, 0)
#define phy_hs2lp_time_clk_lane     DSI_HOST_BITS(0x098, 10, 16) 
#define phy_hs2hs_time_clk_lane     DSI_HOST_BITS(0x098, 10, 0) 
#define phy_hs2lp_time 				DSI_HOST_BITS(0x09c, 8, 24)
#define phy_lp2hs_time 				DSI_HOST_BITS(0x09c, 8, 16)
#define max_rd_time 				DSI_HOST_BITS(0x09c, 15, 0)
#define phy_forcepll 				DSI_HOST_BITS(0x0a0, 1, 3)		//new Dependency: DSI_HOST_FPGA = 0. Otherwise, this bit is reserved.
#define phy_enableclk 				DSI_HOST_BITS(0x0a0, 1, 2)
#define phy_rstz 					DSI_HOST_BITS(0x0a0, 1, 1) 
#define phy_shutdownz 				DSI_HOST_BITS(0x0a0, 1, 0) 
#define phy_stop_wait_time 			DSI_HOST_BITS(0x0a4, 8, 8)
#define n_lanes 					DSI_HOST_BITS(0x0a4, 2, 0)
#define phy_txexitulpslan 			DSI_HOST_BITS(0x0a8, 1, 3)
#define phy_txrequlpslan 			DSI_HOST_BITS(0x0a8, 1, 2)
#define phy_txexitulpsclk 			DSI_HOST_BITS(0x0a8, 1, 1)
#define phy_txrequlpsclk 			DSI_HOST_BITS(0x0a8, 1, 0)
#define phy_tx_triggers 			DSI_HOST_BITS(0x0ac, 4, 0)

#define phystopstateclklane 		DSI_HOST_BITS(0x0b0, 1, 2)
#define phylock 					DSI_HOST_BITS(0x0b0, 1, 0)
#define phy_testclk 				DSI_HOST_BITS(0x0b4, 1, 1)
#define phy_testclr 				DSI_HOST_BITS(0x0b4, 1, 0)
#define phy_testen  				DSI_HOST_BITS(0x0b8, 1, 16)
#define phy_testdout 				DSI_HOST_BITS(0x0b8, 8, 8)
#define phy_testdin 				DSI_HOST_BITS(0x0b8, 8, 0)

#define PHY_TEST_CTRL1 				DSI_HOST_BITS(0x0b8, 17, 0)
#define PHY_TEST_CTRL0              DSI_HOST_BITS(0x0b4, 2, 0)

#define INT_ST0 					DSI_HOST_BITS(0x0bc, 21, 0)
#define INT_ST1 					DSI_HOST_BITS(0x0c0, 18, 0)
#define INT_MKS0 					DSI_HOST_BITS(0x0c4, 21, 0)
#define INT_MKS1 					DSI_HOST_BITS(0x0c8, 18, 0) 
#define INT_FORCE0 					DSI_HOST_BITS(0x0d8, 21, 0) 
#define INT_FORCE1 					DSI_HOST_BITS(0x0dc, 18, 0) 

#define code_hs_rx_clock            0x34
#define code_hs_rx_lane0            0x44
#define code_hs_rx_lane1            0x54
#define code_hs_rx_lane2            0x84
#define code_hs_rx_lane3            0x94

#define code_pll_input_div_rat      0x17
#define code_pll_loop_div_rat       0x18 
#define code_pll_input_loop_div_rat 0x19 

#define code_hstxdatalanerequsetstatetime   0x70
#define code_hstxdatalanepreparestatetime   0x71
#define code_hstxdatalanehszerostatetime    0x72


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

	struct clk	*refclk; 
	unsigned long iobase;
	void __iomem *membase;
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
	u32 irq;
	unsigned long iobase;
	void __iomem *membase;
};

struct dsi {
	u8 dsi_id;
	u8 lcdc_id;
	u8 vid;
	struct dsi_phy phy;
	struct dsi_host host;
	struct mipi_dsi_ops ops;
	struct mipi_dsi_screen screen;
#ifdef CONFIG_MIPI_DSI_LINUX
	struct clk	*dsi_pclk;
	struct clk	*dsi_pd;
#endif
    struct dentry *debugfs_dir;
	struct platform_device *pdev;
};

int rk_mipi_get_dsi_clk(void);
int rk_mipi_get_dsi_num(void);
int rk_mipi_get_dsi_lane(void);
static int rk32_mipi_power_down_DDR(void);
static int rk32_mipi_power_up_DDR(void);
static void rk32_init_phy_mode(int lcdc_id);


#endif /* end of RK32_MIPI_DSI_H */
