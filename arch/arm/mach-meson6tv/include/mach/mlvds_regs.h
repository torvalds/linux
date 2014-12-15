#ifndef __MLVDS_REGS_H
#define __MLVDS_REGS_H

#define MLVDS_TCON0 0
#define MLVDS_TCON1 1
#define MLVDS_TCON2 2
#define MLVDS_TCON3 3
#define MLVDS_TCON4 4
#define MLVDS_TCON5 5
#define MLVDS_TCON6 6
#define MLVDS_TCON7 7

//the following register function is a little different as before
//but the address is same
//MTCON0-3 is full function, and MTCON4-7 is reduced.
#define MTCON0_1ST_HS_ADDR                         0x1410  //L_STH1_HS_ADDR
#define P_MTCON0_1ST_HS_ADDR 		CBUS_REG_ADDR(MTCON0_1ST_HS_ADDR) 	///../ucode/register.h
#define MTCON0_1ST_HE_ADDR                         0x1411  //L_STH1_HE_ADDR
#define P_MTCON0_1ST_HE_ADDR 		CBUS_REG_ADDR(MTCON0_1ST_HE_ADDR) 	///../ucode/register.h
#define MTCON0_1ST_VS_ADDR                         0x1412  //L_STH1_VS_ADDR
#define P_MTCON0_1ST_VS_ADDR 		CBUS_REG_ADDR(MTCON0_1ST_VS_ADDR) 	///../ucode/register.h
#define MTCON0_1ST_VE_ADDR                         0x1413  //L_STH1_VE_ADDR
#define P_MTCON0_1ST_VE_ADDR 		CBUS_REG_ADDR(MTCON0_1ST_VE_ADDR) 	///../ucode/register.h
#define MTCON0_2ND_HS_ADDR                         0x1414  //L_STH2_HS_ADDR
#define P_MTCON0_2ND_HS_ADDR 		CBUS_REG_ADDR(MTCON0_2ND_HS_ADDR) 	///../ucode/register.h
#define MTCON0_2ND_HE_ADDR                         0x1415  //L_STH2_HE_ADDR
#define P_MTCON0_2ND_HE_ADDR 		CBUS_REG_ADDR(MTCON0_2ND_HE_ADDR) 	///../ucode/register.h
#define MTCON0_2ND_VS_ADDR                         0x1416  //L_STH2_VS_ADDR
#define P_MTCON0_2ND_VS_ADDR 		CBUS_REG_ADDR(MTCON0_2ND_VS_ADDR) 	///../ucode/register.h
#define MTCON0_2ND_VE_ADDR                         0x1417  //L_STH2_VE_ADDR
#define P_MTCON0_2ND_VE_ADDR 		CBUS_REG_ADDR(MTCON0_2ND_VE_ADDR) 	///../ucode/register.h

#define MTCON1_1ST_HS_ADDR                         0x141f  //L_CPV1_HS_ADDR
#define P_MTCON1_1ST_HS_ADDR 		CBUS_REG_ADDR(MTCON1_1ST_HS_ADDR) 	///../ucode/register.h
#define MTCON1_1ST_HE_ADDR                         0x1420  //L_CPV1_HE_ADDR
#define P_MTCON1_1ST_HE_ADDR 		CBUS_REG_ADDR(MTCON1_1ST_HE_ADDR) 	///../ucode/register.h
#define MTCON1_1ST_VS_ADDR                         0x1421  //L_CPV1_VS_ADDR
#define P_MTCON1_1ST_VS_ADDR 		CBUS_REG_ADDR(MTCON1_1ST_VS_ADDR) 	///../ucode/register.h
#define MTCON1_1ST_VE_ADDR                         0x1422  //L_CPV1_VE_ADDR
#define P_MTCON1_1ST_VE_ADDR 		CBUS_REG_ADDR(MTCON1_1ST_VE_ADDR) 	///../ucode/register.h
#define MTCON1_2ND_HS_ADDR                         0x1423  //L_CPV2_HS_ADDR
#define P_MTCON1_2ND_HS_ADDR 		CBUS_REG_ADDR(MTCON1_2ND_HS_ADDR) 	///../ucode/register.h
#define MTCON1_2ND_HE_ADDR                         0x1424  //L_CPV2_HE_ADDR
#define P_MTCON1_2ND_HE_ADDR 		CBUS_REG_ADDR(MTCON1_2ND_HE_ADDR) 	///../ucode/register.h
#define MTCON1_2ND_VS_ADDR                         0x1425  //L_CPV2_VS_ADDR
#define P_MTCON1_2ND_VS_ADDR 		CBUS_REG_ADDR(MTCON1_2ND_VS_ADDR) 	///../ucode/register.h
#define MTCON1_2ND_VE_ADDR                         0x1426  //L_CPV2_VE_ADDR
#define P_MTCON1_2ND_VE_ADDR 		CBUS_REG_ADDR(MTCON1_2ND_VE_ADDR) 	///../ucode/register.h

#define MTCON2_1ST_HS_ADDR                         0x1427  //L_STV1_HS_ADDR
#define P_MTCON2_1ST_HS_ADDR 		CBUS_REG_ADDR(MTCON2_1ST_HS_ADDR) 	///../ucode/register.h
#define MTCON2_1ST_HE_ADDR                         0x1428  //L_STV1_HE_ADDR
#define P_MTCON2_1ST_HE_ADDR 		CBUS_REG_ADDR(MTCON2_1ST_HE_ADDR) 	///../ucode/register.h
#define MTCON2_1ST_VS_ADDR                         0x1429  //L_STV1_VS_ADDR
#define P_MTCON2_1ST_VS_ADDR 		CBUS_REG_ADDR(MTCON2_1ST_VS_ADDR) 	///../ucode/register.h
#define MTCON2_1ST_VE_ADDR                         0x142a  //L_STV1_VE_ADDR
#define P_MTCON2_1ST_VE_ADDR 		CBUS_REG_ADDR(MTCON2_1ST_VE_ADDR) 	///../ucode/register.h
#define MTCON2_2ND_HS_ADDR                         0x142b  //L_STV2_HS_ADDR
#define P_MTCON2_2ND_HS_ADDR 		CBUS_REG_ADDR(MTCON2_2ND_HS_ADDR) 	///../ucode/register.h
#define MTCON2_2ND_HE_ADDR                         0x142c  //L_STV2_HE_ADDR
#define P_MTCON2_2ND_HE_ADDR 		CBUS_REG_ADDR(MTCON2_2ND_HE_ADDR) 	///../ucode/register.h
#define MTCON2_2ND_VS_ADDR                         0x142d  //L_STV2_VS_ADDR
#define P_MTCON2_2ND_VS_ADDR 		CBUS_REG_ADDR(MTCON2_2ND_VS_ADDR) 	///../ucode/register.h
#define MTCON2_2ND_VE_ADDR                         0x142e  //L_STV2_VE_ADDR
#define P_MTCON2_2ND_VE_ADDR 		CBUS_REG_ADDR(MTCON2_2ND_VE_ADDR) 	///../ucode/register.h

#define MTCON3_1ST_HS_ADDR                         0x142f  //L_OEV1_HS_ADDR
#define P_MTCON3_1ST_HS_ADDR 		CBUS_REG_ADDR(MTCON3_1ST_HS_ADDR) 	///../ucode/register.h
#define MTCON3_1ST_HE_ADDR                         0x1430  //L_OEV1_HE_ADDR
#define P_MTCON3_1ST_HE_ADDR 		CBUS_REG_ADDR(MTCON3_1ST_HE_ADDR) 	///../ucode/register.h
#define MTCON3_1ST_VS_ADDR                         0x1431  //L_OEV1_VS_ADDR
#define P_MTCON3_1ST_VS_ADDR 		CBUS_REG_ADDR(MTCON3_1ST_VS_ADDR) 	///../ucode/register.h
#define MTCON3_1ST_VE_ADDR                         0x1432  //L_OEV1_VE_ADDR
#define P_MTCON3_1ST_VE_ADDR 		CBUS_REG_ADDR(MTCON3_1ST_VE_ADDR) 	///../ucode/register.h
#define MTCON3_2ND_HS_ADDR                         0x1433  //L_OEV2_HS_ADDR
#define P_MTCON3_2ND_HS_ADDR 		CBUS_REG_ADDR(MTCON3_2ND_HS_ADDR) 	///../ucode/register.h
#define MTCON3_2ND_HE_ADDR                         0x1434  //L_OEV2_HE_ADDR
#define P_MTCON3_2ND_HE_ADDR 		CBUS_REG_ADDR(MTCON3_2ND_HE_ADDR) 	///../ucode/register.h
#define MTCON3_2ND_VS_ADDR                         0x1435  //L_OEV2_VS_ADDR
#define P_MTCON3_2ND_VS_ADDR 		CBUS_REG_ADDR(MTCON3_2ND_VS_ADDR) 	///../ucode/register.h
#define MTCON3_2ND_VE_ADDR                         0x1436  //L_OEV2_VE_ADDR
#define P_MTCON3_2ND_VE_ADDR 		CBUS_REG_ADDR(MTCON3_2ND_VE_ADDR) 	///../ucode/register.h

#define MTCON4_1ST_HS_ADDR                         0x1455  //L_HSYNC_HS_ADDR
#define P_MTCON4_1ST_HS_ADDR 		CBUS_REG_ADDR(MTCON4_1ST_HS_ADDR) 	///../ucode/register.h
#define MTCON4_1ST_HE_ADDR                         0x1456  //L_HSYNC_HE_ADDR
#define P_MTCON4_1ST_HE_ADDR 		CBUS_REG_ADDR(MTCON4_1ST_HE_ADDR) 	///../ucode/register.h
#define MTCON4_1ST_VS_ADDR                         0x1457  //L_HSYNC_VS_ADDR
#define P_MTCON4_1ST_VS_ADDR 		CBUS_REG_ADDR(MTCON4_1ST_VS_ADDR) 	///../ucode/register.h
#define MTCON4_1ST_VE_ADDR                         0x1458  //L_HSYNC_VE_ADDR
#define P_MTCON4_1ST_VE_ADDR 		CBUS_REG_ADDR(MTCON4_1ST_VE_ADDR) 	///../ucode/register.h

#define MTCON5_1ST_HS_ADDR                         0x1459  //L_VSYNC_HS_ADDR
#define P_MTCON5_1ST_HS_ADDR 		CBUS_REG_ADDR(MTCON5_1ST_HS_ADDR) 	///../ucode/register.h
#define MTCON5_1ST_HE_ADDR                         0x145a  //L_VSYNC_HE_ADDR
#define P_MTCON5_1ST_HE_ADDR 		CBUS_REG_ADDR(MTCON5_1ST_HE_ADDR) 	///../ucode/register.h
#define MTCON5_1ST_VS_ADDR                         0x145b  //L_VSYNC_VS_ADDR
#define P_MTCON5_1ST_VS_ADDR 		CBUS_REG_ADDR(MTCON5_1ST_VS_ADDR) 	///../ucode/register.h
#define MTCON5_1ST_VE_ADDR                         0x145c  //L_VSYNC_VE_ADDR
#define P_MTCON5_1ST_VE_ADDR 		CBUS_REG_ADDR(MTCON5_1ST_VE_ADDR) 	///../ucode/register.h

#define MTCON6_1ST_HS_ADDR                         0x1418  //L_OEH_HS_ADDR
#define P_MTCON6_1ST_HS_ADDR 		CBUS_REG_ADDR(MTCON6_1ST_HS_ADDR) 	///../ucode/register.h
#define MTCON6_1ST_HE_ADDR                         0x1419  //L_OEH_HE_ADDR
#define P_MTCON6_1ST_HE_ADDR 		CBUS_REG_ADDR(MTCON6_1ST_HE_ADDR) 	///../ucode/register.h
#define MTCON6_1ST_VS_ADDR                         0x141a  //L_OEH_VS_ADDR
#define P_MTCON6_1ST_VS_ADDR 		CBUS_REG_ADDR(MTCON6_1ST_VS_ADDR) 	///../ucode/register.h
#define MTCON6_1ST_VE_ADDR                         0x141b  //L_OEH_VE_ADDR
#define P_MTCON6_1ST_VE_ADDR 		CBUS_REG_ADDR(MTCON6_1ST_VE_ADDR) 	///../ucode/register.h

#define MTCON7_1ST_HS_ADDR                         0x1437  //L_OEV3_HS_ADDR
#define P_MTCON7_1ST_HS_ADDR 		CBUS_REG_ADDR(MTCON7_1ST_HS_ADDR) 	///../ucode/register.h
#define MTCON7_1ST_HE_ADDR                         0x1438  //L_OEV3_HE_ADDR
#define P_MTCON7_1ST_HE_ADDR 		CBUS_REG_ADDR(MTCON7_1ST_HE_ADDR) 	///../ucode/register.h
#define MTCON7_1ST_VS_ADDR                         0x1439  //L_OEV3_VS_ADDR
#define P_MTCON7_1ST_VS_ADDR 		CBUS_REG_ADDR(MTCON7_1ST_VS_ADDR) 	///../ucode/register.h
#define MTCON7_1ST_VE_ADDR                         0x143a  //L_OEV3_VE_ADDR
#define P_MTCON7_1ST_VE_ADDR 		CBUS_REG_ADDR(MTCON7_1ST_VE_ADDR) 	///../ucode/register.h

//#define MLVDS_CONTROL                              0x14c3
   #define     mLVDS_RESERVED  15    // 15
   #define     mLVDS_double_pattern  14    // 14
   #define     mLVDS_ins_reset  8    // 13:8  // each channel has one bit
   #define     mLVDS_dual_gate  7
   #define     mLVDS_bit_num    6    // 0-6Bits, 1-8Bits
   #define     mLVDS_pair_num   5    // 0-3Pairs, 1-6Pairs
   #define     mLVDS_msb_first  4
   #define     mLVDS_PORT_SWAP  3
   #define     mLVDS_MLSB_SWAP  2
   #define     mLVDS_PN_SWAP    1
   #define     mLVDS_en         0

//#define MLVDS_CONFIG_HI                            0x14c7
//#define MLVDS_CONFIG_LO                            0x14c8
   #define     mLVDS_reset_offset         29 // Bit 31:29
   #define     mLVDS_reset_length         23 // Bit 28:23
   #define     mLVDS_config_reserved      20 // Bit 22:20
   #define     mLVDS_reset_start_bit12    19 // Bit 19
   #define     mLVDS_data_write_toggle    18
   #define     mLVDS_data_write_ini       17
   #define     mLVDS_data_latch_1_toggle  16
   #define     mLVDS_data_latch_1_ini     15
   #define     mLVDS_data_latch_0_toggle  14
   #define     mLVDS_data_latch_0_ini     13
   #define     mLVDS_reset_1_select       12 // 0 - same as reset_0, 1 - 1 clock delay of reset_0
   #define     mLVDS_reset_start           0 // Bit 11:0

//#define TCON_DOUBLE_CTL                            0x14c9
   #define     tcon_double_ini          8 // Bit 7:0
   #define     tcon_double_inv          0 // Bit 7:0
//#define TCON_PATTERN_HI                            0x14ca
//#define TCON_PATTERN_LO                            0x14cb
   #define     tcon_pattern_loop_data     16 // Bit 15:0
   #define     tcon_pattern_loop_start    12 // Bit 3:0
   #define     tcon_pattern_loop_end       8 // Bit 3:0
   #define     tcon_pattern_enable         0 // Bit 7:0
//#define TCON_CONTROL_HI                            0x14cc
//#define TCON_CONTROL_LO                            0x14cd
   #define     tcon_pclk_enable           26 // Bit 5:0 (enable pclk on TCON channel 7 to 2)
   #define     tcon_pclk_div              24 // Bit 1:0 (control phy clok divide 2,4,6,8)
   #define     tcon_delay                  0 // Bit 23:0 (3 bit for each channel)

//#define MLVDS_DUAL_GATE_CTL_HI                     0x14fb
//#define MLVDS_DUAL_GATE_CTL_LO                     0x14fc
   #define     mlvds_tcon_field_en        24 // Bit 7:0
   #define     mlvds_dual_gate_reserved   21 // Bit 2:0
   #define     mlvds_scan_mode_start_line_bit12 20 // Bit 0
   #define     mlvds_scan_mode_odd        16 // Bit 3:0
   #define     mlvds_scan_mode_even       12 // Bit 3:0
   #define     mlvds_scan_mode_start_line  0 // Bit 11:0
//#define MLVDS_RESET_CONFIG_HI                      0x14fd
//#define MLVDS_RESET_CONFIG_LO                      0x14fe
   #define     mLVDS_reset_range_enable   31 // Bit 0
   #define     mLVDS_reset_range_inv      30 // Bit 0
   #define     mLVDS_reset_config_res1    29 // Bit 0
   #define     mLVDS_reset_range_line_0   16 // Bit 11:0
   #define     mLVDS_reset_config_res3    13 // Bit 2:0
   #define     mLVDS_reset_range_line_1    0 // Bit 11:0

//#define MLVDS_CLK_CTL_HI                           0x14f4
//#define MLVDS_CLK_CTL_LO                           0x14f5
   #define     mlvds_clk_pattern_reserved 31 // Bit 31
   #define     mpclk_dly                  28 // Bit 2:0
   #define     mpclk_div                  26 // Bit 1:0 (control phy clok divide 2,4,6,8)
   #define     use_mpclk                  25 // Bit 0
   #define     mlvds_clk_half_delay       24 // Bit 0
   #define     mlvds_clk_pattern           0 // Bit 23:0
//#define MLVDS_DUAL_GATE_WR_START                   0x14f6
   #define     mlvds_dual_gate_wr_start    0 // Bit 12:0
//#define MLVDS_DUAL_GATE_WR_END                     0x14f7
   #define     mlvds_dual_gate_wr_end      0 // Bit 12:0
//#define MLVDS_DUAL_GATE_RD_START                   0x14f8
   #define     mlvds_dual_gate_rd_start    0 // Bit 12:0
//#define MLVDS_DUAL_GATE_RD_END                     0x14f9
   #define     mlvds_dual_gate_rd_end      0 // Bit 12:0
//#define MLVDS_SECOND_RESET_CTL                     0x14fa
   #define     mLVDS_2nd_reset_start       0 // Bit 12:0

#endif

