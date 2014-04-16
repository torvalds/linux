/*
drivers/video/rockchip/transmitter/rk616_mipi_dsi.h
*/
#include <linux/rockchip/grf.h>
#ifndef RK616_MIPI_DSI_H
#define RK616_MIPI_DSI_H

#define MIPI_DSI_PHY_OFFSET		0x0C00
#define MIPI_DSI_PHY_SIZE		0x34c
#define MIPI_DSI_HOST_OFFSET	0x1000

#ifdef DWC_DSI_VERSION_0x3131302A
#define MIPI_DSI_HOST_SIZE		0x74
#else
#define MIPI_DSI_HOST_SIZE		0xcc
#endif

//function bits definition    register addr | bits | offest
#define REG_ADDR(a)			((a) << 16)
#define REG_BITS(a)			((a) << 8)
#define BITS_OFFSET(a)		(a)
#define DSI_HOST_BITS(addr, bits, bit_offset)  (REG_ADDR((addr)+MIPI_DSI_HOST_OFFSET) \
		| REG_BITS(bits) | BITS_OFFSET(bit_offset))  
#define DSI_DPHY_BITS(addr, bits, bit_offset)  (REG_ADDR((addr)+MIPI_DSI_PHY_OFFSET) \
		| REG_BITS(bits) | BITS_OFFSET(bit_offset))

#ifdef DWC_DSI_VERSION_0x3131302A

#define VERSION 					DSI_HOST_BITS(0x00, 32, 0)
#define GEN_HDR 					DSI_HOST_BITS(0x34, 32, 0)
#define GEN_PLD_DATA 				DSI_HOST_BITS(0x38, 32, 0)
#define ERROR_ST0 					DSI_HOST_BITS(0x44, 21, 0)
#define ERROR_ST1 					DSI_HOST_BITS(0x48, 18, 0)
#define ERROR_MSK0 					DSI_HOST_BITS(0x4C, 21, 0)
#define ERROR_MSK1 					DSI_HOST_BITS(0x50, 18, 0)

#define shutdownz 					DSI_HOST_BITS(0x04, 1, 0)
#define en18_loosely 				DSI_HOST_BITS(0x0c, 1, 10)
#define colorm_active_low 			DSI_HOST_BITS(0x0c, 1, 9)
#define shutd_active_low  			DSI_HOST_BITS(0x0c, 1, 8)
#define hsync_active_low  			DSI_HOST_BITS(0x0c, 1, 7)
#define vsync_active_low  			DSI_HOST_BITS(0x0c, 1, 6)
#define dataen_active_low  			DSI_HOST_BITS(0x0c, 1, 5)
#define dpi_color_coding 			DSI_HOST_BITS(0x0c, 3, 2)
#define dpi_vcid					DSI_HOST_BITS(0x0c, 1, 0)
#define vid_hline_time  			DSI_HOST_BITS(0x28, 14, 18)
#define vid_hbp_time 				DSI_HOST_BITS(0x28, 9, 9)
#define vid_hsa_time 				DSI_HOST_BITS(0x28, 9, 0)
#define vid_active_lines 			DSI_HOST_BITS(0x2c, 11, 16)
#define vid_vfp_lines 				DSI_HOST_BITS(0x2c, 6, 10)
#define vid_vbp_lines 				DSI_HOST_BITS(0x2c, 6, 4)
#define vid_vsa_lines 				DSI_HOST_BITS(0x2c, 4, 0)
#define TO_CLK_DIVISION 			DSI_HOST_BITS(0x08, 8, 8)
#define TX_ESC_CLK_DIVISION 		DSI_HOST_BITS(0x08, 8, 0)
#define gen_vid_rx 					DSI_HOST_BITS(0x18, 2, 5)
#define crc_rx_en 					DSI_HOST_BITS(0x18, 1, 4)
#define ecc_rx_en 					DSI_HOST_BITS(0x18, 1, 3)
#define bta_en 						DSI_HOST_BITS(0x18, 1, 2)
#define eotp_rx_en 					DSI_HOST_BITS(0x18, 1, 1)
#define eotp_tx_en 					DSI_HOST_BITS(0x18, 1, 0)
#define lp_cmd_en  					DSI_HOST_BITS(0x1c, 1, 12)
#define frame_bta_ack_en 			DSI_HOST_BITS(0x1c, 1, 11)
#define en_null_pkt 				DSI_HOST_BITS(0x1c, 1, 10)
#define en_multi_pkt 				DSI_HOST_BITS(0x1c, 1, 9)
#define lp_hfp_en 					DSI_HOST_BITS(0x1c, 1, 8)
#define lp_hbp_en 					DSI_HOST_BITS(0x1c, 1, 7)
#define lp_vact_en 					DSI_HOST_BITS(0x1c, 1, 6)
#define lp_vfp_en 					DSI_HOST_BITS(0x1c, 1, 5)
#define lp_vbp_en 					DSI_HOST_BITS(0x1c, 1, 4)
#define lp_vsa_en 					DSI_HOST_BITS(0x1c, 1, 3)
#define vid_mode_type 				DSI_HOST_BITS(0x1c, 2, 1)
#define en_video_mode 				DSI_HOST_BITS(0x1c, 1, 0)
#define null_pkt_size 				DSI_HOST_BITS(0x20, 10, 21)
#define num_chunks 					DSI_HOST_BITS(0x20, 10, 11)
#define vid_pkt_size 				DSI_HOST_BITS(0x20, 11, 0)
#define tear_fx_en 					DSI_HOST_BITS(0x24, 1, 14)
#define ack_rqst_en 				DSI_HOST_BITS(0x24, 1, 13)
#define dcs_lw_tx 					DSI_HOST_BITS(0x24, 1, 12)
#define gen_lw_tx 					DSI_HOST_BITS(0x24, 1, 11)
#define max_rd_pkt_size 			DSI_HOST_BITS(0x24, 1, 10)
#define dcs_sr_0p_tx 				DSI_HOST_BITS(0x24, 1, 9)
#define dcs_sw_1p_tx 				DSI_HOST_BITS(0x24, 1, 8)
#define dcs_sw_0p_tx				DSI_HOST_BITS(0x24, 1, 7)
#define gen_sr_2p_tx				DSI_HOST_BITS(0x24, 1, 6)
#define gen_sr_1p_tx 				DSI_HOST_BITS(0x24, 1, 5)
#define gen_sr_0p_tx 				DSI_HOST_BITS(0x24, 1, 4)
#define gen_sw_2p_tx 				DSI_HOST_BITS(0x24, 1, 3)
#define gen_sw_1p_tx 				DSI_HOST_BITS(0x24, 1, 2)
#define gen_sw_0p_tx 				DSI_HOST_BITS(0x24, 1, 1)
#define en_cmd_mode 				DSI_HOST_BITS(0x24, 1, 0)
#define phy_hs2lp_time 				DSI_HOST_BITS(0x30, 8, 24)
#define phy_lp2hs_time 				DSI_HOST_BITS(0x30, 8, 16)
#define max_rd_time 				DSI_HOST_BITS(0x30, 15, 0)
#define lprx_to_cnt 				DSI_HOST_BITS(0x40, 16, 16)
#define hstx_to_cnt 				DSI_HOST_BITS(0x40, 16, 0)
#define phy_enableclk 				DSI_HOST_BITS(0x54, 1, 2)
//#define phy_rstz 					DSI_HOST_BITS(0x54, 1, 1)
//#define phy_shutdownz 				DSI_HOST_BITS(0x54, 1, 0)

#define phy_stop_wait_time 			DSI_HOST_BITS(0x58, 8, 2)
#define n_lanes 					DSI_HOST_BITS(0x58, 2, 0)
#define phy_tx_triggers 			DSI_HOST_BITS(0x5c, 4, 5)
#define phy_txexitulpslan 			DSI_HOST_BITS(0x5c, 1, 4)
#define phy_txrequlpslan 			DSI_HOST_BITS(0x5c, 1, 3)
#define phy_txexitulpsclk 			DSI_HOST_BITS(0x5c, 1, 2)
#define phy_txrequlpsclk 			DSI_HOST_BITS(0x5c, 1, 1)
#define phy_txrequestclkhs 			DSI_HOST_BITS(0x5c, 1, 0)
#define phy_testclk 				DSI_HOST_BITS(0x64, 1, 1)
#define phy_testclr 				DSI_HOST_BITS(0x64, 1, 0)
#define phy_testen  				DSI_HOST_BITS(0x68, 1, 16)
#define phy_testdout 				DSI_HOST_BITS(0x68, 8, 8)
#define phy_testdin 				DSI_HOST_BITS(0x68, 8, 0)
#define outvact_lpcmd_time  		DSI_HOST_BITS(0x70, 8, 8)
#define invact_lpcmd_time 			DSI_HOST_BITS(0x70, 8, 0)
#define gen_rd_cmd_busy  			DSI_HOST_BITS(0x3c, 1, 6)
#define gen_pld_r_full 				DSI_HOST_BITS(0x3c, 1, 5)
#define gen_pld_r_empty 			DSI_HOST_BITS(0x3c, 1, 4)
#define gen_pld_w_full 				DSI_HOST_BITS(0x3c, 1, 3)     //800byte    write GEN_PLD_DATA
#define gen_pld_w_empty 			DSI_HOST_BITS(0x3c, 1, 2)
#define gen_cmd_full 				DSI_HOST_BITS(0x3c, 1, 1)     //20   write GEN_HDR
#define gen_cmd_empty 				DSI_HOST_BITS(0x3c, 1, 0)
#define phystopstateclklane 		DSI_HOST_BITS(0x60, 1, 2)
#define phylock 					DSI_HOST_BITS(0x60, 1, 0)

#else  //***************************************************************//
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
#define gen_vid_rx 					DSI_HOST_BITS(0x030, 2, 0) //libing (0x030, 2, 5)-> (0x030, 2, 0)
#define cmd_video_mode 				DSI_HOST_BITS(0x034, 1, 0)
#define vpg_orientation             DSI_HOST_BITS(0x038, 1, 24) //libing 
#define vpg_mode                    DSI_HOST_BITS(0x038, 1, 20) //libing 
#define vpg_en                      DSI_HOST_BITS(0x038, 1, 16) //libing 
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
#define phy_hs2lp_time_clk_lane     DSI_HOST_BITS(0x098, 10, 16) //libing
#define phy_hs2hs_time_clk_lane     DSI_HOST_BITS(0x098, 10, 0) //libing
#define phy_hs2lp_time 				DSI_HOST_BITS(0x09c, 8, 24)
#define phy_lp2hs_time 				DSI_HOST_BITS(0x09c, 8, 16)
#define max_rd_time 				DSI_HOST_BITS(0x09c, 15, 0)
#define phy_forcepll 				DSI_HOST_BITS(0x0a0, 1, 3)		//new Dependency: DSI_HOST_FPGA = 0. Otherwise, this bit is reserved.
#define phy_enableclk 				DSI_HOST_BITS(0x0a0, 1, 2)
#define phy_rstz 					DSI_HOST_BITS(0x0a0, 1, 1)  //libing
#define phy_shutdownz 				DSI_HOST_BITS(0x0a0, 1, 0) //libing 
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
#define INT_MKS1 					DSI_HOST_BITS(0x0c8, 18, 0) //libing
#define INT_FORCE0 					DSI_HOST_BITS(0x0d8, 21, 0) //libing
#define INT_FORCE1 					DSI_HOST_BITS(0x0dc, 18, 0) //libing

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





//#define en_null_pkt				DSI_HOST_BITS(0x1c, 1, 13)  //delete
//#define en_multi_pkt				DSI_HOST_BITS(0x1c, 1, 13)  //delete
#endif  /* end of DWC_DSI_VERSION_0x3131302A */



//MIPI DSI DPHY REGISTERS
#define DPHY_REGISTER0				DSI_DPHY_BITS(0x00, 32, 0)
#define DPHY_REGISTER1				DSI_DPHY_BITS(0x04, 32, 0)
#define DPHY_REGISTER3				DSI_DPHY_BITS(0x0c, 32, 0)
#define DPHY_REGISTER4				DSI_DPHY_BITS(0x10, 32, 0)
#define DPHY_REGISTER20				DSI_DPHY_BITS(0X80, 32, 0)

#define lane_en_ck 					DSI_DPHY_BITS(0x00, 1, 6)
#define lane_en_3 					DSI_DPHY_BITS(0x00, 1, 5)
#define lane_en_2 					DSI_DPHY_BITS(0x00, 1, 4)
#define lane_en_1 					DSI_DPHY_BITS(0x00, 1, 3)
#define lane_en_0 					DSI_DPHY_BITS(0x00, 1, 2)

#define reg_da_ppfc 				DSI_DPHY_BITS(0x04, 1, 4)
#define reg_da_syncrst 				DSI_DPHY_BITS(0x04, 1, 2)
#define reg_da_ldopd 				DSI_DPHY_BITS(0x04, 1, 1)
#define reg_da_pllpd 				DSI_DPHY_BITS(0x04, 1, 0)

#define reg_fbdiv_8 				DSI_DPHY_BITS(0x0c, 1, 5)
#define reg_prediv 					DSI_DPHY_BITS(0x0c, 5, 0)
#define reg_fbdiv 					DSI_DPHY_BITS(0x10, 8, 0)

#define reg_dig_rstn 				DSI_DPHY_BITS(0X80, 1, 0)

#define DPHY_CLOCK_OFFSET			REG_ADDR(0X0100)
#define DPHY_LANE0_OFFSET			REG_ADDR(0X0180)
#define DPHY_LANE1_OFFSET			REG_ADDR(0X0200)
#define DPHY_LANE2_OFFSET			REG_ADDR(0X0280)
#define DPHY_LANE3_OFFSET			REG_ADDR(0X0300)

#define reg_ths_settle				DSI_DPHY_BITS(0x0000, 4, 0)
#define reg_hs_tlpx					DSI_DPHY_BITS(0x0014, 6, 0)
#define reg_hs_ths_prepare			DSI_DPHY_BITS(0x0018, 7, 0)
#define reg_hs_the_zero				DSI_DPHY_BITS(0x001c, 6, 0)
#define reg_hs_ths_trail			DSI_DPHY_BITS(0x0020, 7, 0)
#define reg_hs_ths_exit				DSI_DPHY_BITS(0x0024, 5, 0)
#define reg_hs_tclk_post			DSI_DPHY_BITS(0x0028, 4, 0)
#define reserved					DSI_DPHY_BITS(0x002c, 1, 0)
#define reg_hs_twakup_h				DSI_DPHY_BITS(0x0030, 2, 0)
#define reg_hs_twakup_l				DSI_DPHY_BITS(0x0034, 8, 0)
#define reg_hs_tclk_pre				DSI_DPHY_BITS(0x0038, 4, 0)
#define reg_hs_tta_go				DSI_DPHY_BITS(0x0040, 6, 0)
#define reg_hs_tta_sure				DSI_DPHY_BITS(0x0044, 6, 0)
#define reg_hs_tta_wait				DSI_DPHY_BITS(0x0048, 6, 0)


#ifdef DWC_DSI_VERSION_0x3131302A
//MISC REGISTERS
#define DSI_MISC_BITS(addr, bits, bit_offset)  (REG_ADDR(addr) \
		| REG_BITS(bits) | BITS_OFFSET(bit_offset))

#define CRU_CRU_CLKSEL1_CON			(0x005c)
#define CRU_CFG_MISC_CON			(0x009c)

#define cfg_mipiclk_gaten 			DSI_MISC_BITS(CRU_CRU_CLKSEL1_CON, 1, 10)

#define mipi_int 					DSI_MISC_BITS(CRU_CFG_MISC_CON, 1, 19)
#define mipi_edpihalt 				DSI_MISC_BITS(CRU_CFG_MISC_CON, 1, 16)
#define pin_forcetxstopmode_3 		DSI_MISC_BITS(CRU_CFG_MISC_CON, 1, 11)
#define pin_forcetxstopmode_2 		DSI_MISC_BITS(CRU_CFG_MISC_CON, 1, 10)
#define pin_forcetxstopmode_1 		DSI_MISC_BITS(CRU_CFG_MISC_CON, 1, 9)
#define pin_forcetxstopmode_0 		DSI_MISC_BITS(CRU_CFG_MISC_CON, 1, 8)
#define pin_forcerxmode_0 			DSI_MISC_BITS(CRU_CFG_MISC_CON, 1, 7)
#define pin_turndisable_0 			DSI_MISC_BITS(CRU_CFG_MISC_CON, 1, 6)
#define dpicolom 					DSI_MISC_BITS(CRU_CFG_MISC_CON, 1, 2)
#define dpishutdn 					DSI_MISC_BITS(CRU_CFG_MISC_CON, 1, 1)

#else

//#define mipi_int 					
//#define mipi_edpihalt 				
#define pin_forcetxstopmode_3 		
#define pin_forcetxstopmode_2 		
#define pin_forcetxstopmode_1 		
#define pin_forcetxstopmode_0 		
#define pin_forcerxmode_0 			
#define pin_turndisable_0 			
#define dpicolom 					
#define dpishutdn 					

#endif


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

#ifdef CONFIG_MIPI_DSI_LINUX
	struct clk	*refclk; 
	unsigned long iobase;
	void __iomem *membase;
#endif	
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
#ifdef CONFIG_MIPI_DSI_LINUX
	unsigned long iobase;
	void __iomem *membase;
#endif
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
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
#endif
    struct dentry *debugfs_dir;
	struct platform_device *pdev;
};

int rk_mipi_get_dsi_clk(void);
int rk_mipi_get_dsi_num(void);
int rk_mipi_get_dsi_lane(void);

extern int rk616_mipi_dsi_ft_init(void);
int rk_mipi_dsi_init_lite(struct dsi *dsi);
#endif /* end of RK616_MIPI_DSI_H */
