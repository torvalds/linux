/* 
 * pwm-sunxi.h 
 * 
 * (C) Copyright 2013 
 * David H. Wilkins  <dwil...@conecuh.com> 
 * 
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of 
 * the License, or (at your option) any later version. 
 * 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.         See the 
 * GNU General Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 * MA 02111-1307 USA 
 */ 
 
 
#define SUN4I_PWM_IOREG_MAX 10 
#define SUN4I_MAX_HARDWARE_PWM_CHANNELS 2 
 
/* 
 * structure that defines the pwm control register 
 */ 
 
enum sun4i_pwm_prescale { 
	PRESCALE_DIV120  = 0x00,  /* Divide 24mhz clock by 120 */ 
	PRESCALE_DIV180  = 0x01, 
	PRESCALE_DIV240  = 0x02, 
	PRESCALE_DIV360  = 0x03, 
	PRESCALE_DIV480  = 0x04, 
	PRESCALE_INVx05  = 0x05, 
	PRESCALE_INVx06  = 0x06, 
	PRESCALE_INVx07  = 0x07, 
	PRESCALE_DIV12k  = 0x08, 
	PRESCALE_DIV24k  = 0x09, 
	PRESCALE_DIV36k  = 0x0a, 
	PRESCALE_DIV48k  = 0x0b, 
	PRESCALE_DIV72k  = 0x0c 
}; 
 
 
 
 
struct sun4i_pwm_ctrl { 
	enum sun4i_pwm_prescale ch0_prescaler:4; /* ch0 Prescale register - values above */ 
	unsigned int ch0_en:1;                  /* chan 0 enable */ 
	unsigned int ch0_act_state:1;           /* chan 0 polarity 0=low, 1=high */ 
	unsigned int ch0_clk_gating:1;          /* Allow clock to run for chan 0 */ 
	unsigned int ch0_mode:1;                /* Mode - 0 = cycle(running), 1=only 1 pulse */ 
	unsigned int ch0_pulse_start:1;         /* Write 1 for mode pulse above to start */ 
	unsigned int unused1:6;                 /* The bit skip count is 6 */ 
	enum sun4i_pwm_prescale ch1_prescaler:4; /* ch1 Prescale register - values above*/ 
	unsigned int ch1_en:1;                  /* chan 1 enable */ 
	unsigned int ch1_act_state:1;           /* chan 1 polarity 0=low, 1=high */ 
	unsigned int ch1_clk_gating:1;          /* Allow clock to run for chan 1 */ 
	unsigned int ch1_mode:1;                /* Mode - 0 = cycle(running), 1=only 1 pulse */ 
	unsigned int ch1_pulse_start:1;         /* Write 1 for mode pulse above to start */ 
	unsigned int unused2:6;                 /* The bit skip count is 6 */ 
}; 
 
 
#define A10CLK 24000000  /* Speed of the clock - 24mhz */ 
 
#define NO_ENABLE_CHANGE 2  /* Signal to set_pwm_mode to keep the same chan enable bit */ 
 
#define PWM_CTRL_ENABLE 1 
#define PWM_CTRL_DISABLE 0 
 
#define MAX_CYCLES 0x0ffff /* max cycle count possible for period active and entire */ 
struct sun4i_pwm_period { 
#if MAX_CYCLES > 0x0ff 
	unsigned int pwm_active_cycles:16;        /* duty cycle */ 
	unsigned int pwm_entire_cycles:16;        /* period */ 
#else 
	unsigned int pwm_active_cycles:8;        /* duty cycle */ 
	unsigned int unused1:8; 
	unsigned int pwm_entire_cycles:8;        /* period */ 
	unsigned int unused2:8; 
#endif 
}; 
 
 
enum sun4i_ioreg_pin_select { 
	SELECT_INPUT      = 0x00,                /* bits for the config registers */ 
	SELECT_OUTPUT     = 0x01, 
	SELECT_PWM        = 0x02, 
	SELECT_SPI2_CLK   = 0x02, 
	SELECT_I2S_LRCK   = 0x02, 
	SELECT_I2S_BCLK   = 0x02 
}; 
 
 
struct sun4i_ioreg_cfg0 { 
	enum sun4i_ioreg_pin_select pin0_select:4; 
	enum sun4i_ioreg_pin_select pin1_select:4; 
	enum sun4i_ioreg_pin_select pin2_select:4; 
	enum sun4i_ioreg_pin_select pin3_select:4; 
	enum sun4i_ioreg_pin_select pin4_select:4; 
	enum sun4i_ioreg_pin_select pin5_select:4; 
	enum sun4i_ioreg_pin_select pin6_select:4; 
	enum sun4i_ioreg_pin_select pin7_select:4; 
}; 
 
/* 
 * another duplicate struct to make the pin names 
 * look right 
 */ 
struct sun4i_ioreg_cfg1 { 
	enum  sun4i_ioreg_pin_select pin8_select:4; 
	enum  sun4i_ioreg_pin_select pin9_select:4; 
	enum  sun4i_ioreg_pin_select pin10_select:4; 
	enum  sun4i_ioreg_pin_select pin11_select:4; 
	enum  sun4i_ioreg_pin_select pin12_select:4; 
	enum  sun4i_ioreg_pin_select pin13_select:4; 
	enum  sun4i_ioreg_pin_select pin14_select:4; 
	enum  sun4i_ioreg_pin_select pin15_select:4; 
}; 
 
 
struct ioreg_pull { 
	unsigned int pin0:2; 
	unsigned int pin1:2; 
	unsigned int pin2:2; 
	unsigned int pin3:2; 
	unsigned int pin4:2; 
	unsigned int pin5:2; 
	unsigned int pin6:2; 
	unsigned int pin7:2; 
	unsigned int pin8:2; 
	unsigned int pin9:2; 
	unsigned int pin10:2; 
	unsigned int pin11:2; 
	unsigned int pin12:2; 
	unsigned int pin13:2; 
	unsigned int pin14:2; 
	unsigned int pin15:2; 
}; 
 
union ioreg_pull_u { 
	struct ioreg_pull s; 
	unsigned int initializer; 
}; 
 
 
union sun4i_pwm_ctrl_u { 
	struct sun4i_pwm_ctrl s; 
	unsigned int initializer; 
}; 
 
union sun4i_pwm_period_u { 
	struct sun4i_pwm_period s; 
	unsigned int initializer; 
}; 
 
union sun4i_ioreg_cfg_u { 
	struct sun4i_ioreg_cfg0 s0; /* io register config 0 */ 
	struct sun4i_ioreg_cfg1 s1; /* io register config 1 (just to make pin names look nice) */ 
	unsigned int initializer; 
}; 
 
 
 
struct sun4i_pwm_available_channel{ 
	unsigned int use_count; 
	void *ctrl_addr;                           /* Address of the control register */ 
	void *pin_addr;                            /* Address of the pin register to change to PWM mode */ 
	void *period_reg_addr;                     /* Address of the period register for this chan */ 
	unsigned int channel;                      /* Channel number */ 
	unsigned long period;                      /* Period in microseconds */ 
	unsigned long duty;                        /* duty cycle in microseconds */ 
	unsigned int duty_percent;                 /* percentage (drives duty microseconds if set) */ 
	enum sun4i_pwm_prescale prescale;           /* best prescale value computed for period */ 
	union sun4i_pwm_period_u period_reg;       /* period register */ 
	union sun4i_pwm_ctrl_u ctrl_backup;        /* control register backup at init */ 
	union sun4i_pwm_ctrl_u ctrl_mask;          /* mask for ctrl register bit we can change */ 
	union sun4i_pwm_ctrl_u ctrl_current;       /* current control register settings */ 
	union sun4i_ioreg_cfg_u pin_backup;        /* pin backup at init */ 
	union sun4i_ioreg_cfg_u pin_mask;          /* mask of pin settings we can change */ 
	union sun4i_ioreg_cfg_u pin_current;       /* current pin register */ 
	const char *pin_name;                      /* name of the pin */ 
	const char *name;                          /* name of the pwm device from the pwm i/f */ 
}; 
 
/* 
 * struct used to implement the hz/khz/ms/us etc for period and duty 
 */ 
struct time_suffix { 
	char * suffix;                             /* text suffix */ 
	unsigned long multiplier;                  /* multiplier for the entered value */ 
	bool freq;                                 /* true if a frequency, otherwise a time */ 
	                                           /* T = 1/f and f = 1/T */ 
};
