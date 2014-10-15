#ifndef _TVAFE_GENERAL_H
#define _TVAFE_GENERAL_H

#include <linux/amlogic/tvin/tvin.h>
#include "tvafe_cvd.h"

// ***************************************************************************
// *** macro definitions *********************************************
// ***************************************************************************

#define LOG_ADC_CAL
//#define LOG_VGA_EDID
// ***************************************************************************
// *** enum definitions *********************************************
// ***************************************************************************
typedef enum tvafe_adc_ch_e {
	TVAFE_ADC_CH_NULL = 0,
	TVAFE_ADC_CH_PGA,
	TVAFE_ADC_CH_A,
	TVAFE_ADC_CH_B,
	TVAFE_ADC_CH_C,
} tvafe_adc_ch_t;

// ***************************************************************************
// *** structure definitions *********************************************
// ***************************************************************************
typedef struct tvafe_cal_operand_s {
	unsigned int a;
	unsigned int b;
	unsigned int c;
	unsigned int step;
	unsigned int bpg_h;
	unsigned int bpg_v;
	unsigned int clk_ctl;
	unsigned int vafe_ctl;
#ifdef CONFIG_ADC_CAL_SIGNALED	
	unsigned int pin_a_mux:2;
	unsigned int pin_b_mux:3;
	unsigned int pin_c_mux:3;
	unsigned int sog_mux  :3;
#endif
	unsigned int sync_mux :1;
	unsigned int clk_ext  :1;
	unsigned int bpg_m    :2;
	unsigned int lpf_a    :1;
	unsigned int lpf_b    :1;
	unsigned int lpf_c    :1;
	unsigned int clamp_inv:1;
	unsigned int clamp_ext:1;
	unsigned int adj      :1;
	unsigned int cnt      :3;
	unsigned int dir      :1;
	unsigned int dir0     :1;
	unsigned int dir1     :1;
	unsigned int dir2     :1;
	unsigned int adc0;
	unsigned int adc1;
	unsigned int adc2;
	unsigned int data0;
	unsigned int data1;
	unsigned int data2;
	unsigned int cal_fmt_cnt;
	unsigned int cal_fmt_max;
} tvafe_cal_operand_t;

typedef struct tvafe_cal_s {
	//adc calibration data
	struct tvafe_adc_cal_s      cal_val;
    struct tvafe_adc_comp_cal_s  fmt_cal_val;
	struct tvafe_cal_operand_s  cal_operand;
} tvafe_cal_t;

// *****************************************************************************
// ******** function claims ********
// *****************************************************************************
extern int  tvafe_set_source_muxing(enum tvin_port_e port, struct tvafe_pin_mux_s *pinmux);
extern void tvafe_vga_set_edid(struct tvafe_vga_edid_s *edid);
extern void tvafe_vga_get_edid(struct tvafe_vga_edid_s *edid);
extern void tvafe_set_cal_value(struct tvafe_cal_s *cal);
extern int tvafe_get_cal_value(struct tvafe_cal_s *cal);
extern void tvafe_set_cal_value2(struct tvafe_adc_cal_s *cal);
extern void tvafe_set_regmap(struct am_regs_s *p);
extern int tvafe_get_cal_value2(struct tvafe_adc_cal_s *cal);
extern bool tvafe_adc_cal(struct tvin_parm_s *parm, struct tvafe_cal_s *cal);
extern void tvafe_adc_clamp_adjust(struct tvin_parm_s *parm, struct tvafe_cal_s *cal);
extern void tvafe_get_wss_data(struct tvafe_comp_wss_s *wss);
extern void tvafe_set_vga_fmt(struct tvin_parm_s *parm, struct tvafe_cal_s *cal, struct tvafe_pin_mux_s *pinmux);
extern void tvafe_set_comp_fmt(struct tvin_parm_s *parm, struct tvafe_cal_s *cal, struct tvafe_pin_mux_s *pinmux);
extern void tvafe_init_reg(struct tvafe_cvd2_s *cvd2, struct tvafe_cvd2_mem_s *mem, enum tvin_port_e port, struct tvafe_pin_mux_s *pinmux);
extern void tvafe_set_apb_bus_err_ctrl(void);
extern void tvafe_enable_module(bool enable);
extern void tvafe_enable_avout(bool enable);
#endif  // _TVAFE_GENERAL_H

