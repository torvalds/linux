/*
 * linux/drivers/video/w100fb.h
 *
 * Frame Buffer Device for ATI w100 (Wallaby)
 *
 * Copyright (C) 2002, ATI Corp.
 * Copyright (C) 2004-2005 Richard Purdie
 * Copyright (c) 2005 Ian Molton <spyro@f2s.com>
 *
 * Modified to work with 2.6 by Richard Purdie <rpurdie@rpsys.net>
 *
 * w32xx support by Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#if !defined (_W100FB_H)
#define _W100FB_H

/* Block CIF Start: */
#define mmCHIP_ID           0x0000
#define mmREVISION_ID       0x0004
#define mmWRAP_BUF_A        0x0008
#define mmWRAP_BUF_B        0x000C
#define mmWRAP_TOP_DIR      0x0010
#define mmWRAP_START_DIR    0x0014
#define mmCIF_CNTL          0x0018
#define mmCFGREG_BASE       0x001C
#define mmCIF_IO            0x0020
#define mmCIF_READ_DBG      0x0024
#define mmCIF_WRITE_DBG     0x0028
#define cfgIND_ADDR_A_0     0x0000
#define cfgIND_ADDR_A_1     0x0001
#define cfgIND_ADDR_A_2     0x0002
#define cfgIND_DATA_A       0x0003
#define cfgREG_BASE         0x0004
#define cfgINTF_CNTL        0x0005
#define cfgSTATUS           0x0006
#define cfgCPU_DEFAULTS     0x0007
#define cfgIND_ADDR_B_0     0x0008
#define cfgIND_ADDR_B_1     0x0009
#define cfgIND_ADDR_B_2     0x000A
#define cfgIND_DATA_B       0x000B
#define cfgPM4_RPTR         0x000C
#define cfgSCRATCH          0x000D
#define cfgPM4_WRPTR_0      0x000E
#define cfgPM4_WRPTR_1      0x000F
/* Block CIF End: */

/* Block CP Start: */
#define mmSCRATCH_UMSK      0x0280
#define mmSCRATCH_ADDR      0x0284
#define mmGEN_INT_CNTL      0x0200
#define mmGEN_INT_STATUS    0x0204
/* Block CP End: */

/* Block DISPLAY Start: */
#define mmLCD_FORMAT        0x0410
#define mmGRAPHIC_CTRL      0x0414
#define mmGRAPHIC_OFFSET    0x0418
#define mmGRAPHIC_PITCH     0x041C
#define mmCRTC_TOTAL        0x0420
#define mmACTIVE_H_DISP     0x0424
#define mmACTIVE_V_DISP     0x0428
#define mmGRAPHIC_H_DISP    0x042C
#define mmGRAPHIC_V_DISP    0x0430
#define mmVIDEO_CTRL        0x0434
#define mmGRAPHIC_KEY       0x0438
#define mmBRIGHTNESS_CNTL   0x045C
#define mmDISP_INT_CNTL     0x0488
#define mmCRTC_SS           0x048C
#define mmCRTC_LS           0x0490
#define mmCRTC_REV          0x0494
#define mmCRTC_DCLK         0x049C
#define mmCRTC_GS           0x04A0
#define mmCRTC_VPOS_GS      0x04A4
#define mmCRTC_GCLK         0x04A8
#define mmCRTC_GOE          0x04AC
#define mmCRTC_FRAME        0x04B0
#define mmCRTC_FRAME_VPOS   0x04B4
#define mmGPIO_DATA         0x04B8
#define mmGPIO_CNTL1        0x04BC
#define mmGPIO_CNTL2        0x04C0
#define mmLCDD_CNTL1        0x04C4
#define mmLCDD_CNTL2        0x04C8
#define mmGENLCD_CNTL1      0x04CC
#define mmGENLCD_CNTL2      0x04D0
#define mmDISP_DEBUG        0x04D4
#define mmDISP_DB_BUF_CNTL  0x04D8
#define mmDISP_CRC_SIG      0x04DC
#define mmCRTC_DEFAULT_COUNT    0x04E0
#define mmLCD_BACKGROUND_COLOR  0x04E4
#define mmCRTC_PS2          0x04E8
#define mmCRTC_PS2_VPOS     0x04EC
#define mmCRTC_PS1_ACTIVE   0x04F0
#define mmCRTC_PS1_NACTIVE  0x04F4
#define mmCRTC_GCLK_EXT     0x04F8
#define mmCRTC_ALW          0x04FC
#define mmCRTC_ALW_VPOS     0x0500
#define mmCRTC_PSK          0x0504
#define mmCRTC_PSK_HPOS     0x0508
#define mmCRTC_CV4_START    0x050C
#define mmCRTC_CV4_END      0x0510
#define mmCRTC_CV4_HPOS     0x0514
#define mmCRTC_ECK          0x051C
#define mmREFRESH_CNTL      0x0520
#define mmGENLCD_CNTL3      0x0524
#define mmGPIO_DATA2        0x0528
#define mmGPIO_CNTL3        0x052C
#define mmGPIO_CNTL4        0x0530
#define mmCHIP_STRAP        0x0534
#define mmDISP_DEBUG2       0x0538
#define mmDEBUG_BUS_CNTL    0x053C
#define mmGAMMA_VALUE1      0x0540
#define mmGAMMA_VALUE2      0x0544
#define mmGAMMA_SLOPE       0x0548
#define mmGEN_STATUS        0x054C
#define mmHW_INT            0x0550
/* Block DISPLAY End: */

/* Block GFX Start: */
#define mmDST_OFFSET          0x1004
#define mmDST_PITCH           0x1008
#define mmDST_Y_X             0x1038
#define mmDST_WIDTH_HEIGHT    0x1198
#define mmDP_GUI_MASTER_CNTL  0x106C
#define mmBRUSH_OFFSET        0x108C
#define mmBRUSH_Y_X           0x1074
#define mmDP_BRUSH_FRGD_CLR   0x107C
#define mmSRC_OFFSET          0x11AC
#define mmSRC_PITCH           0x11B0
#define mmSRC_Y_X             0x1034
#define mmDEFAULT_PITCH_OFFSET      0x10A0
#define mmDEFAULT_SC_BOTTOM_RIGHT   0x10A8
#define mmDEFAULT2_SC_BOTTOM_RIGHT  0x10AC
#define mmSC_TOP_LEFT         0x11BC
#define mmSC_BOTTOM_RIGHT     0x11C0
#define mmSRC_SC_BOTTOM_RIGHT 0x11C4
#define mmGLOBAL_ALPHA        0x1210
#define mmFILTER_COEF         0x1214
#define mmMVC_CNTL_START      0x11E0
#define mmE2_ARITHMETIC_CNTL  0x1220
#define mmDP_CNTL             0x11C8
#define mmDP_CNTL_DST_DIR     0x11CC
#define mmDP_DATATYPE         0x12C4
#define mmDP_MIX              0x12C8
#define mmDP_WRITE_MSK        0x12CC
#define mmENG_CNTL            0x13E8
#define mmENG_PERF_CNT        0x13F0
/* Block GFX End: */

/* Block IDCT Start: */
#define mmIDCT_RUNS         0x0C00
#define mmIDCT_LEVELS       0x0C04
#define mmIDCT_CONTROL      0x0C3C
#define mmIDCT_AUTH_CONTROL 0x0C08
#define mmIDCT_AUTH         0x0C0C
/* Block IDCT End: */

/* Block MC Start: */
#define mmMEM_CNTL             0x0180
#define mmMEM_ARB              0x0184
#define mmMC_FB_LOCATION       0x0188
#define mmMEM_EXT_CNTL         0x018C
#define mmMC_EXT_MEM_LOCATION  0x0190
#define mmMEM_EXT_TIMING_CNTL  0x0194
#define mmMEM_SDRAM_MODE_REG   0x0198
#define mmMEM_IO_CNTL          0x019C
#define mmMC_DEBUG             0x01A0
#define mmMC_BIST_CTRL         0x01A4
#define mmMC_BIST_COLLAR_READ  0x01A8
#define mmTC_MISMATCH          0x01AC
#define mmMC_PERF_MON_CNTL     0x01B0
#define mmMC_PERF_COUNTERS     0x01B4
/* Block MC End: */

/* Block BM Start: */
#define mmBM_EXT_MEM_BANDWIDTH    0x0A00
#define mmBM_OFFSET               0x0A04
#define mmBM_MEM_EXT_TIMING_CNTL  0x0A08
#define mmBM_MEM_EXT_CNTL         0x0A0C
#define mmBM_MEM_MODE_REG         0x0A10
#define mmBM_MEM_IO_CNTL          0x0A18
#define mmBM_CONFIG               0x0A1C
#define mmBM_STATUS               0x0A20
#define mmBM_DEBUG                0x0A24
#define mmBM_PERF_MON_CNTL        0x0A28
#define mmBM_PERF_COUNTERS        0x0A2C
#define mmBM_PERF2_MON_CNTL       0x0A30
#define mmBM_PERF2_COUNTERS       0x0A34
/* Block BM End: */

/* Block RBBM Start: */
#define mmWAIT_UNTIL        0x1400
#define mmISYNC_CNTL        0x1404
#define mmRBBM_STATUS       0x0140
#define mmRBBM_CNTL         0x0144
#define mmNQWAIT_UNTIL      0x0150
/* Block RBBM End: */

/* Block CG Start: */
#define mmCLK_PIN_CNTL      0x0080
#define mmPLL_REF_FB_DIV    0x0084
#define mmPLL_CNTL          0x0088
#define mmSCLK_CNTL         0x008C
#define mmPCLK_CNTL         0x0090
#define mmCLK_TEST_CNTL     0x0094
#define mmPWRMGT_CNTL       0x0098
#define mmPWRMGT_STATUS     0x009C
/* Block CG End: */

/* default value definitions */
#define defWRAP_TOP_DIR        0x00000000
#define defWRAP_START_DIR      0x00000000
#define defCFGREG_BASE         0x00000000
#define defCIF_IO              0x000C0902
#define defINTF_CNTL           0x00000011
#define defCPU_DEFAULTS        0x00000006
#define defHW_INT              0x00000000
#define defMC_EXT_MEM_LOCATION 0x07ff0000
#define defTC_MISMATCH         0x00000000

#define W100_CFG_BASE          0x0
#define W100_CFG_LEN           0x10
#define W100_REG_BASE          0x10000
#define W100_REG_LEN           0x2000
#define MEM_INT_BASE_VALUE     0x100000
#define MEM_EXT_BASE_VALUE     0x800000
#define MEM_INT_SIZE           0x05ffff
#define MEM_WINDOW_BASE        0x100000
#define MEM_WINDOW_SIZE        0xf00000

#define WRAP_BUF_BASE_VALUE    0x80000
#define WRAP_BUF_TOP_VALUE     0xbffff

#define CHIP_ID_W100           0x57411002
#define CHIP_ID_W3200          0x56441002
#define CHIP_ID_W3220          0x57441002

/* Register structure definitions */

struct wrap_top_dir_t {
	u32 top_addr  : 23;
	u32           : 9;
} __attribute__((packed));

union wrap_top_dir_u {
	u32 val : 32;
	struct wrap_top_dir_t f;
} __attribute__((packed));

struct wrap_start_dir_t {
	u32 start_addr : 23;
	u32            : 9;
} __attribute__((packed));

union wrap_start_dir_u {
	u32 val : 32;
	struct wrap_start_dir_t f;
} __attribute__((packed));

struct cif_cntl_t {
	u32 swap_reg                 : 2;
	u32 swap_fbuf_1              : 2;
	u32 swap_fbuf_2              : 2;
	u32 swap_fbuf_3              : 2;
	u32 pmi_int_disable          : 1;
	u32 pmi_schmen_disable       : 1;
	u32 intb_oe                  : 1;
	u32 en_wait_to_compensate_dq_prop_dly  : 1;
	u32 compensate_wait_rd_size  : 2;
	u32 wait_asserted_timeout_val  : 2;
	u32 wait_masked_val          : 2;
	u32 en_wait_timeout          : 1;
	u32 en_one_clk_setup_before_wait  : 1;
	u32 interrupt_active_high    : 1;
	u32 en_overwrite_straps      : 1;
	u32 strap_wait_active_hi     : 1;
	u32 lat_busy_count           : 2;
	u32 lat_rd_pm4_sclk_busy     : 1;
	u32 dis_system_bits          : 1;
	u32 dis_mr                   : 1;
	u32 cif_spare_1              : 4;
} __attribute__((packed));

union cif_cntl_u {
	u32 val : 32;
	struct cif_cntl_t f;
} __attribute__((packed));

struct cfgreg_base_t {
	u32 cfgreg_base  : 24;
	u32              : 8;
} __attribute__((packed));

union cfgreg_base_u {
	u32 val : 32;
	struct cfgreg_base_t f;
} __attribute__((packed));

struct cif_io_t {
	u32 dq_srp     : 1;
	u32 dq_srn     : 1;
	u32 dq_sp      : 4;
	u32 dq_sn      : 4;
	u32 waitb_srp  : 1;
	u32 waitb_srn  : 1;
	u32 waitb_sp   : 4;
	u32 waitb_sn   : 4;
	u32 intb_srp   : 1;
	u32 intb_srn   : 1;
	u32 intb_sp    : 4;
	u32 intb_sn    : 4;
	u32            : 2;
} __attribute__((packed));

union cif_io_u {
	u32 val : 32;
	struct cif_io_t f;
} __attribute__((packed));

struct cif_read_dbg_t {
	u32 unpacker_pre_fetch_trig_gen  : 2;
	u32 dly_second_rd_fetch_trig     : 1;
	u32 rst_rd_burst_id              : 1;
	u32 dis_rd_burst_id              : 1;
	u32 en_block_rd_when_packer_is_not_emp : 1;
	u32 dis_pre_fetch_cntl_sm        : 1;
	u32 rbbm_chrncy_dis              : 1;
	u32 rbbm_rd_after_wr_lat         : 2;
	u32 dis_be_during_rd             : 1;
	u32 one_clk_invalidate_pulse     : 1;
	u32 dis_chnl_priority            : 1;
	u32 rst_read_path_a_pls          : 1;
	u32 rst_read_path_b_pls          : 1;
	u32 dis_reg_rd_fetch_trig        : 1;
	u32 dis_rd_fetch_trig_from_ind_addr : 1;
	u32 dis_rd_same_byte_to_trig_fetch : 1;
	u32 dis_dir_wrap                 : 1;
	u32 dis_ring_buf_to_force_dec    : 1;
	u32 dis_addr_comp_in_16bit       : 1;
	u32 clr_w                        : 1;
	u32 err_rd_tag_is_3              : 1;
	u32 err_load_when_ful_a          : 1;
	u32 err_load_when_ful_b          : 1;
	u32                              : 7;
} __attribute__((packed));

union cif_read_dbg_u {
	u32 val : 32;
	struct cif_read_dbg_t f;
} __attribute__((packed));

struct cif_write_dbg_t {
	u32 packer_timeout_count          : 2;
	u32 en_upper_load_cond            : 1;
	u32 en_chnl_change_cond           : 1;
	u32 dis_addr_comp_cond            : 1;
	u32 dis_load_same_byte_addr_cond  : 1;
	u32 dis_timeout_cond              : 1;
	u32 dis_timeout_during_rbbm       : 1;
	u32 dis_packer_ful_during_rbbm_timeout : 1;
	u32 en_dword_split_to_rbbm        : 1;
	u32 en_dummy_val                  : 1;
	u32 dummy_val_sel                 : 1;
	u32 mask_pm4_wrptr_dec            : 1;
	u32 dis_mc_clean_cond             : 1;
	u32 err_two_reqi_during_ful       : 1;
	u32 err_reqi_during_idle_clk      : 1;
	u32 err_global                    : 1;
	u32 en_wr_buf_dbg_load            : 1;
	u32 en_wr_buf_dbg_path            : 1;
	u32 sel_wr_buf_byte               : 3;
	u32 dis_rd_flush_wr               : 1;
	u32 dis_packer_ful_cond           : 1;
	u32 dis_invalidate_by_ops_chnl    : 1;
	u32 en_halt_when_reqi_err         : 1;
	u32 cif_spare_2                   : 5;
	u32                               : 1;
} __attribute__((packed));

union cif_write_dbg_u {
	u32 val : 32;
	struct cif_write_dbg_t f;
} __attribute__((packed));


struct intf_cntl_t {
	unsigned char ad_inc_a            : 1;
	unsigned char ring_buf_a          : 1;
	unsigned char rd_fetch_trigger_a  : 1;
	unsigned char rd_data_rdy_a       : 1;
	unsigned char ad_inc_b            : 1;
	unsigned char ring_buf_b          : 1;
	unsigned char rd_fetch_trigger_b  : 1;
	unsigned char rd_data_rdy_b       : 1;
} __attribute__((packed));

union intf_cntl_u {
	unsigned char val : 8;
	struct intf_cntl_t f;
} __attribute__((packed));

struct cpu_defaults_t {
	unsigned char unpack_rd_data     : 1;
	unsigned char access_ind_addr_a  : 1;
	unsigned char access_ind_addr_b  : 1;
	unsigned char access_scratch_reg : 1;
	unsigned char pack_wr_data       : 1;
	unsigned char transition_size    : 1;
	unsigned char en_read_buf_mode   : 1;
	unsigned char rd_fetch_scratch   : 1;
} __attribute__((packed));

union cpu_defaults_u {
	unsigned char val : 8;
	struct cpu_defaults_t f;
} __attribute__((packed));

struct crtc_total_t {
	u32 crtc_h_total : 10;
	u32              : 6;
	u32 crtc_v_total : 10;
	u32              : 6;
} __attribute__((packed));

union crtc_total_u {
	u32 val : 32;
	struct crtc_total_t f;
} __attribute__((packed));

struct crtc_ss_t {
	u32 ss_start    : 10;
	u32             : 6;
	u32 ss_end      : 10;
	u32             : 2;
	u32 ss_align    : 1;
	u32 ss_pol      : 1;
	u32 ss_run_mode : 1;
	u32 ss_en       : 1;
} __attribute__((packed));

union crtc_ss_u {
	u32 val : 32;
	struct crtc_ss_t f;
} __attribute__((packed));

struct active_h_disp_t {
	u32 active_h_start  : 10;
	u32                 : 6;
	u32 active_h_end    : 10;
	u32                 : 6;
} __attribute__((packed));

union active_h_disp_u {
	u32 val : 32;
	struct active_h_disp_t f;
} __attribute__((packed));

struct active_v_disp_t {
	u32 active_v_start  : 10;
	u32                 : 6;
	u32 active_v_end    : 10;
	u32                 : 6;
} __attribute__((packed));

union active_v_disp_u {
	u32 val : 32;
	struct active_v_disp_t f;
} __attribute__((packed));

struct graphic_h_disp_t {
	u32 graphic_h_start : 10;
	u32                 : 6;
	u32 graphic_h_end   : 10;
	u32                 : 6;
} __attribute__((packed));

union graphic_h_disp_u {
	u32 val : 32;
	struct graphic_h_disp_t f;
} __attribute__((packed));

struct graphic_v_disp_t {
	u32 graphic_v_start : 10;
	u32                 : 6;
	u32 graphic_v_end   : 10;
	u32                 : 6;
} __attribute__((packed));

union graphic_v_disp_u{
	u32 val : 32;
	struct graphic_v_disp_t f;
} __attribute__((packed));

struct graphic_ctrl_t_w100 {
	u32 color_depth       : 3;
	u32 portrait_mode     : 2;
	u32 low_power_on      : 1;
	u32 req_freq          : 4;
	u32 en_crtc           : 1;
	u32 en_graphic_req    : 1;
	u32 en_graphic_crtc   : 1;
	u32 total_req_graphic : 9;
	u32 lcd_pclk_on       : 1;
	u32 lcd_sclk_on       : 1;
	u32 pclk_running      : 1;
	u32 sclk_running      : 1;
	u32                   : 6;
} __attribute__((packed));

struct graphic_ctrl_t_w32xx {
	u32 color_depth       : 3;
	u32 portrait_mode     : 2;
	u32 low_power_on      : 1;
	u32 req_freq          : 4;
	u32 en_crtc           : 1;
	u32 en_graphic_req    : 1;
	u32 en_graphic_crtc   : 1;
	u32 total_req_graphic : 10;
	u32 lcd_pclk_on       : 1;
	u32 lcd_sclk_on       : 1;
	u32 pclk_running      : 1;
	u32 sclk_running      : 1;
	u32                   : 5;
} __attribute__((packed));

union graphic_ctrl_u {
	u32 val : 32;
	struct graphic_ctrl_t_w100 f_w100;
	struct graphic_ctrl_t_w32xx f_w32xx;
} __attribute__((packed));

struct video_ctrl_t {
	u32 video_mode       : 1;
	u32 keyer_en         : 1;
	u32 en_video_req     : 1;
	u32 en_graphic_req_video  : 1;
	u32 en_video_crtc    : 1;
	u32 video_hor_exp    : 2;
	u32 video_ver_exp    : 2;
	u32 uv_combine       : 1;
	u32 total_req_video  : 9;
	u32 video_ch_sel     : 1;
	u32 video_portrait   : 2;
	u32 yuv2rgb_en       : 1;
	u32 yuv2rgb_option   : 1;
	u32 video_inv_hor    : 1;
	u32 video_inv_ver    : 1;
	u32 gamma_sel        : 2;
	u32 dis_limit        : 1;
	u32 en_uv_hblend     : 1;
	u32 rgb_gamma_sel    : 2;
} __attribute__((packed));

union video_ctrl_u {
	u32 val : 32;
	struct video_ctrl_t f;
} __attribute__((packed));

struct disp_db_buf_cntl_rd_t {
	u32 en_db_buf           : 1;
	u32 update_db_buf_done  : 1;
	u32 db_buf_cntl         : 6;
	u32                     : 24;
} __attribute__((packed));

union disp_db_buf_cntl_rd_u {
	u32 val : 32;
	struct disp_db_buf_cntl_rd_t f;
} __attribute__((packed));

struct disp_db_buf_cntl_wr_t {
	u32 en_db_buf      : 1;
	u32 update_db_buf  : 1;
	u32 db_buf_cntl    : 6;
	u32                : 24;
} __attribute__((packed));

union disp_db_buf_cntl_wr_u {
	u32 val : 32;
	struct disp_db_buf_cntl_wr_t f;
} __attribute__((packed));

struct gamma_value1_t {
	u32 gamma1   : 8;
	u32 gamma2   : 8;
	u32 gamma3   : 8;
	u32 gamma4   : 8;
} __attribute__((packed));

union gamma_value1_u {
	u32 val : 32;
	struct gamma_value1_t f;
} __attribute__((packed));

struct gamma_value2_t {
	u32 gamma5   : 8;
	u32 gamma6   : 8;
	u32 gamma7   : 8;
	u32 gamma8   : 8;
} __attribute__((packed));

union gamma_value2_u {
	u32 val : 32;
	struct gamma_value2_t f;
} __attribute__((packed));

struct gamma_slope_t {
	u32 slope1   : 3;
	u32 slope2   : 3;
	u32 slope3   : 3;
	u32 slope4   : 3;
	u32 slope5   : 3;
	u32 slope6   : 3;
	u32 slope7   : 3;
	u32 slope8   : 3;
	u32          : 8;
} __attribute__((packed));

union gamma_slope_u {
	u32 val : 32;
	struct gamma_slope_t f;
} __attribute__((packed));

struct mc_ext_mem_location_t {
	u32 mc_ext_mem_start : 16;
	u32 mc_ext_mem_top   : 16;
} __attribute__((packed));

union mc_ext_mem_location_u {
	u32 val : 32;
	struct mc_ext_mem_location_t f;
} __attribute__((packed));

struct mc_fb_location_t {
	u32 mc_fb_start      : 16;
	u32 mc_fb_top        : 16;
} __attribute__((packed));

union mc_fb_location_u {
	u32 val : 32;
	struct mc_fb_location_t f;
} __attribute__((packed));

struct clk_pin_cntl_t {
	u32 osc_en           : 1;
	u32 osc_gain         : 5;
	u32 dont_use_xtalin  : 1;
	u32 xtalin_pm_en     : 1;
	u32 xtalin_dbl_en    : 1;
	u32                  : 7;
	u32 cg_debug         : 16;
} __attribute__((packed));

union clk_pin_cntl_u {
	u32 val : 32;
	struct clk_pin_cntl_t f;
} __attribute__((packed));

struct pll_ref_fb_div_t {
	u32 pll_ref_div      : 4;
	u32                  : 4;
	u32 pll_fb_div_int   : 6;
	u32                  : 2;
	u32 pll_fb_div_frac  : 3;
	u32                  : 1;
	u32 pll_reset_time   : 4;
	u32 pll_lock_time    : 8;
} __attribute__((packed));

union pll_ref_fb_div_u {
	u32 val : 32;
	struct pll_ref_fb_div_t f;
} __attribute__((packed));

struct pll_cntl_t {
	u32 pll_pwdn        : 1;
	u32 pll_reset       : 1;
	u32 pll_pm_en       : 1;
	u32 pll_mode        : 1;
	u32 pll_refclk_sel  : 1;
	u32 pll_fbclk_sel   : 1;
	u32 pll_tcpoff      : 1;
	u32 pll_pcp         : 3;
	u32 pll_pvg         : 3;
	u32 pll_vcofr       : 1;
	u32 pll_ioffset     : 2;
	u32 pll_pecc_mode   : 2;
	u32 pll_pecc_scon   : 2;
	u32 pll_dactal      : 4;
	u32 pll_cp_clip     : 2;
	u32 pll_conf        : 3;
	u32 pll_mbctrl      : 2;
	u32 pll_ring_off    : 1;
} __attribute__((packed));

union pll_cntl_u {
	u32 val : 32;
	struct pll_cntl_t f;
} __attribute__((packed));

struct sclk_cntl_t {
	u32 sclk_src_sel         : 2;
	u32                      : 2;
	u32 sclk_post_div_fast   : 4;
	u32 sclk_clkon_hys       : 3;
	u32 sclk_post_div_slow   : 4;
	u32 disp_cg_ok2switch_en : 1;
	u32 sclk_force_reg       : 1;
	u32 sclk_force_disp      : 1;
	u32 sclk_force_mc        : 1;
	u32 sclk_force_extmc     : 1;
	u32 sclk_force_cp        : 1;
	u32 sclk_force_e2        : 1;
	u32 sclk_force_e3        : 1;
	u32 sclk_force_idct      : 1;
	u32 sclk_force_bist      : 1;
	u32 busy_extend_cp       : 1;
	u32 busy_extend_e2       : 1;
	u32 busy_extend_e3       : 1;
	u32 busy_extend_idct     : 1;
	u32                      : 3;
} __attribute__((packed));

union sclk_cntl_u {
	u32 val : 32;
	struct sclk_cntl_t f;
} __attribute__((packed));

struct pclk_cntl_t {
	u32 pclk_src_sel     : 2;
	u32                  : 2;
	u32 pclk_post_div    : 4;
	u32                  : 8;
	u32 pclk_force_disp  : 1;
	u32                  : 15;
} __attribute__((packed));

union pclk_cntl_u {
	u32 val : 32;
	struct pclk_cntl_t f;
} __attribute__((packed));


#define TESTCLK_SRC_PLL   0x01
#define TESTCLK_SRC_SCLK  0x02
#define TESTCLK_SRC_PCLK  0x03
/* 4 and 5 seem to by XTAL/M */
#define TESTCLK_SRC_XTAL  0x06

struct clk_test_cntl_t {
	u32 testclk_sel      : 4;
	u32                  : 3;
	u32 start_check_freq : 1;
	u32 tstcount_rst     : 1;
	u32                  : 15;
	u32 test_count       : 8;
} __attribute__((packed));

union clk_test_cntl_u {
	u32 val : 32;
	struct clk_test_cntl_t f;
} __attribute__((packed));

struct pwrmgt_cntl_t {
	u32 pwm_enable           : 1;
	u32                      : 1;
	u32 pwm_mode_req         : 2;
	u32 pwm_wakeup_cond      : 2;
	u32 pwm_fast_noml_hw_en  : 1;
	u32 pwm_noml_fast_hw_en  : 1;
	u32 pwm_fast_noml_cond   : 4;
	u32 pwm_noml_fast_cond   : 4;
	u32 pwm_idle_timer       : 8;
	u32 pwm_busy_timer       : 8;
} __attribute__((packed));

union pwrmgt_cntl_u {
	u32 val : 32;
	struct pwrmgt_cntl_t f;
} __attribute__((packed));

#define SRC_DATATYPE_EQU_DST	3

#define ROP3_SRCCOPY	0xcc
#define ROP3_PATCOPY	0xf0

#define GMC_BRUSH_SOLID_COLOR	13
#define GMC_BRUSH_NONE			15

#define DP_SRC_MEM_RECTANGULAR	2

#define DP_OP_ROP	0

struct dp_gui_master_cntl_t {
	u32 gmc_src_pitch_offset_cntl : 1;
	u32 gmc_dst_pitch_offset_cntl : 1;
	u32 gmc_src_clipping          : 1;
	u32 gmc_dst_clipping          : 1;
	u32 gmc_brush_datatype        : 4;
	u32 gmc_dst_datatype          : 4;
	u32 gmc_src_datatype          : 3;
	u32 gmc_byte_pix_order        : 1;
	u32 gmc_default_sel           : 1;
	u32 gmc_rop3                  : 8;
	u32 gmc_dp_src_source         : 3;
	u32 gmc_clr_cmp_fcn_dis       : 1;
	u32                           : 1;
	u32 gmc_wr_msk_dis            : 1;
	u32 gmc_dp_op                 : 1;
} __attribute__((packed));

union dp_gui_master_cntl_u {
	u32 val : 32;
	struct dp_gui_master_cntl_t f;
} __attribute__((packed));

struct rbbm_status_t {
	u32 cmdfifo_avail   : 7;
	u32                 : 1;
	u32 hirq_on_rbb     : 1;
	u32 cprq_on_rbb     : 1;
	u32 cfrq_on_rbb     : 1;
	u32 hirq_in_rtbuf   : 1;
	u32 cprq_in_rtbuf   : 1;
	u32 cfrq_in_rtbuf   : 1;
	u32 cf_pipe_busy    : 1;
	u32 eng_ev_busy     : 1;
	u32 cp_cmdstrm_busy : 1;
	u32 e2_busy         : 1;
	u32 rb2d_busy       : 1;
	u32 rb3d_busy       : 1;
	u32 se_busy         : 1;
	u32 re_busy         : 1;
	u32 tam_busy        : 1;
	u32 tdm_busy        : 1;
	u32 pb_busy         : 1;
	u32                 : 6;
	u32 gui_active      : 1;
} __attribute__((packed));

union rbbm_status_u {
	u32 val : 32;
	struct rbbm_status_t f;
} __attribute__((packed));

struct dp_datatype_t {
	u32 dp_dst_datatype   : 4;
	u32                   : 4;
	u32 dp_brush_datatype : 4;
	u32 dp_src2_type      : 1;
	u32 dp_src2_datatype  : 3;
	u32 dp_src_datatype   : 3;
	u32                   : 11;
	u32 dp_byte_pix_order : 1;
	u32                   : 1;
} __attribute__((packed));

union dp_datatype_u {
	u32 val : 32;
	struct dp_datatype_t f;
} __attribute__((packed));

struct dp_mix_t {
	u32                : 8;
	u32 dp_src_source  : 3;
	u32 dp_src2_source : 3;
	u32                : 2;
	u32 dp_rop3        : 8;
	u32 dp_op          : 1;
	u32                : 7;
} __attribute__((packed));

union dp_mix_u {
	u32 val : 32;
	struct dp_mix_t f;
} __attribute__((packed));

struct eng_cntl_t {
	u32 erc_reg_rd_ws            : 1;
	u32 erc_reg_wr_ws            : 1;
	u32 erc_idle_reg_wr          : 1;
	u32 dis_engine_triggers      : 1;
	u32 dis_rop_src_uses_dst_w_h : 1;
	u32 dis_src_uses_dst_dirmaj  : 1;
	u32                          : 6;
	u32 force_3dclk_when_2dclk   : 1;
	u32                          : 19;
} __attribute__((packed));

union eng_cntl_u {
	u32 val : 32;
	struct eng_cntl_t f;
} __attribute__((packed));

struct dp_cntl_t {
	u32 dst_x_dir   : 1;
	u32 dst_y_dir   : 1;
	u32 src_x_dir   : 1;
	u32 src_y_dir   : 1;
	u32 dst_major_x : 1;
	u32 src_major_x : 1;
	u32             : 26;
} __attribute__((packed));

union dp_cntl_u {
	u32 val : 32;
	struct dp_cntl_t f;
} __attribute__((packed));

struct dp_cntl_dst_dir_t {
	u32           : 15;
	u32 dst_y_dir : 1;
	u32           : 15;
	u32 dst_x_dir : 1;
} __attribute__((packed));

union dp_cntl_dst_dir_u {
	u32 val : 32;
	struct dp_cntl_dst_dir_t f;
} __attribute__((packed));

#endif

