/*******************************************************************
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *  File name: TVAFE_CVD.h
 *  Description: IO function, structure, enum, used in TVIN AFE sub-module processing
 *******************************************************************/
#ifndef _TVAFE_CVD_H
#define _TVAFE_CVD_H

#include <linux/amlogic/tvin/tvin.h>

/***************************Local defines**********************************/
/* cvd2 memory size defines */
#define DECODER_MOTION_BUFFER_ADDR_OFFSET   0x70000
#define DECODER_MOTION_BUFFER_4F_LENGTH     0x15a60
#define DECODER_VBI_ADDR_OFFSET             0x86000
#define DECODER_VBI_VBI_SIZE                0x1000
#define DECODER_VBI_START_ADDR              0x0

/* cvd2 function enable/disable defines*/
//#define TVAFE_CVD2_NOT_TRUST_NOSIG  // Do not trust Reg no signal flag
//#define SYNC_HEIGHT_AUTO_TUNING
//#define TVAFE_CVD2_ADC_REG_CHECK

/* cvd2 function enable/disable defines*/
//#define TVAFE_CVD2_TUNER_DET_ACCELERATED  // accelerate tuner mode detection

/* cvd2 VBI function enable/disable defines*/
//#define TVAFE_CVD2_CC_ENABLE  // enable cvd2 close caption

/* cvd2 auto adjust de enable/disable defines*/
#define TVAFE_CVD2_AUTO_DE_ENABLE                  // enable cvd2 de auto ajust
#define TVAFE_CVD2_AUTO_DE_CHECK_CNT        100    // check lines counter 100*10ms
#define TVAFE_CVD2_AUTO_DE_TH               0xd0   // audo de threshold
#define TVAFE_CVD2_PAL_DE_START             0x17   // default de start value for pal


// ***************************************************************************
// *** enum definitions *********************************************
// ***************************************************************************
typedef enum tvafe_cvd2_state_e {
	TVAFE_CVD2_STATE_INIT = 0,
	TVAFE_CVD2_STATE_FIND,
} tvafe_cvd2_state_t;

// ***************************************************************************
// *** structure definitions *********************************************
// ***************************************************************************
typedef struct tvafe_cvd2_hw_data_s {
	bool no_sig;
	bool h_lock;
	bool v_lock;
	bool h_nonstd;
	bool v_nonstd;
	bool no_color_burst;
	bool comb3d_off;
	bool chroma_lock;
	bool pal;
	bool secam;
	bool line625;
	bool noisy;
	bool vcr;
	bool vcrtrick;
	bool vcrff;
	bool vcrrew;
	unsigned char cordic;

	unsigned char acc4xx_cnt;
	unsigned char acc425_cnt;
	unsigned char acc3xx_cnt;
	unsigned char acc358_cnt;
	bool secam_detected;
	bool secam_phase;
	bool fsc_358;
	bool fsc_425;
	bool fsc_443;

} tvafe_cvd2_hw_data_t;

/* cvd2 memory */
typedef struct tvafe_cvd2_mem_s {
	unsigned int                start;  //memory start addr for cvd2 module
	unsigned int                size;   //memory size for cvd2 module
} tvafe_cvd2_mem_t;

#ifdef TVAFE_CVD2_AUTO_DE_ENABLE
typedef struct tvafe_cvd2_lines_s {
	unsigned int                val[4];
	unsigned int                check_cnt;
	unsigned int                de_offset;
} tvafe_cvd2_lines_t;
#endif

/* cvd2 signal information */
typedef struct tvafe_cvd2_info_s {
	enum tvafe_cvd2_state_e     state;
	unsigned int                state_cnt;
#ifdef TVAFE_SET_CVBS_CDTO_EN
	unsigned int                hcnt64[4];
	unsigned int                hcnt64_cnt;
#endif
#ifdef TVAFE_SET_CVBS_PGA_EN
	unsigned short              dgain[4];
	unsigned short              dgain_cnt;
#endif
	unsigned int                comb_check_cnt;
	unsigned int                fmt_shift_cnt;
	bool                        non_std_enable;
	bool                        non_std_config;
	bool                        non_std_worst;
	bool                        adc_reload_en;


#ifdef TVAFE_CVD2_ADC_REG_CHECK
	unsigned int                normal_cnt;
#endif

#ifdef TVAFE_CVD2_AUTO_DE_ENABLE
	struct tvafe_cvd2_lines_s   vlines;
#endif
	unsigned int                ntsc_switch_cnt;
} tvafe_cvd2_info_t;

//CVD2 status list
typedef struct tvafe_cvd2_s {
	struct tvafe_cvd2_hw_data_s hw_data[3];
	struct tvafe_cvd2_hw_data_s hw;
	unsigned char               hw_data_cur;
	enum tvin_port_e            vd_port;
	bool                        cvd2_init_en;
	enum tvin_sig_fmt_e         config_fmt;
	enum tvin_sig_fmt_e         manual_fmt;
	unsigned int                fmt_loop_cnt;
	struct tvafe_cvd2_info_s    info;
} tvafe_cvd2_t;

// *****************************************************************************
// ******** GLOBAL FUNCTION CLAIM ********
// *****************************************************************************
extern int cvd_get_rf_strength(void);

extern void tvafe_cvd2_try_format(struct tvafe_cvd2_s *cvd2, struct tvafe_cvd2_mem_s *mem, enum tvin_sig_fmt_e fmt);
extern bool tvafe_cvd2_no_sig(struct tvafe_cvd2_s *cvd2, struct tvafe_cvd2_mem_s *mem);
extern bool tvafe_cvd2_fmt_chg(struct tvafe_cvd2_s *cvd2);
extern enum tvin_sig_fmt_e tvafe_cvd2_get_format(struct tvafe_cvd2_s *cvd2);
#ifdef TVAFE_SET_CVBS_PGA_EN
extern void tvafe_cvd2_adj_pga(struct tvafe_cvd2_s *cvd2);
#endif
#ifdef TVAFE_SET_CVBS_CDTO_EN
extern void tvafe_cvd2_adj_cdto(struct tvafe_cvd2_s *cvd2, unsigned int hcnt64);
#endif
extern void tvafe_cvd2_set_default_cdto(struct tvafe_cvd2_s *cvd2);
extern void tvafe_cvd2_set_default_de(struct tvafe_cvd2_s *cvd2);
extern void tvafe_cvd2_check_3d_comb(struct tvafe_cvd2_s *cvd2);
extern void tvafe_cvd2_reset_pga(void);
extern enum tvafe_cvbs_video_e tvafe_cvd2_get_lock_status(struct tvafe_cvd2_s *cvd2);
extern int tvafe_cvd2_get_atv_format(void);
extern int tvafe_cvd2_get_hv_lock(void);
extern void tvafe_cvd2_hold_rst(struct tvafe_cvd2_s *cvd2);
extern void tvafe_cvd2_set_reg8a(unsigned int v);
extern void get_cvd_version(char **ver,char **last_ver);

#endif // _TVAFE_CVD_H

