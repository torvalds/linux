#ifndef DIB8000_H
#define DIB8000_H

#include "dibx000_common.h"

struct dib8000_config {
	u8 output_mpeg2_in_188_bytes;
	u8 hostbus_diversity;
	u8 tuner_is_baseband;
	int (*update_lna) (struct dvb_frontend *, u16 agc_global);

	u8 agc_config_count;
	struct dibx000_agc_config *agc;
	struct dibx000_bandwidth_config *pll;

#define DIB8000_GPIO_DEFAULT_DIRECTIONS 0xffff
	u16 gpio_dir;
#define DIB8000_GPIO_DEFAULT_VALUES     0x0000
	u16 gpio_val;
#define DIB8000_GPIO_PWM_POS0(v)        ((v & 0xf) << 12)
#define DIB8000_GPIO_PWM_POS1(v)        ((v & 0xf) << 8 )
#define DIB8000_GPIO_PWM_POS2(v)        ((v & 0xf) << 4 )
#define DIB8000_GPIO_PWM_POS3(v)         (v & 0xf)
#define DIB8000_GPIO_DEFAULT_PWM_POS    0xffff
	u16 gpio_pwm_pos;
	u16 pwm_freq_div;

	void (*agc_control) (struct dvb_frontend *, u8 before);

	u16 drives;
	u16 diversity_delay;
	u8 div_cfg;
	u8 output_mode;
	u8 refclksel;
	u8 enMpegOutput:1;

	struct dibx000_bandwidth_config *plltable;
};

#define DEFAULT_DIB8000_I2C_ADDRESS 18

struct dib8000_ops {
	int (*set_wbd_ref)(struct dvb_frontend *fe, u16 value);
	int (*update_pll)(struct dvb_frontend *fe,
		struct dibx000_bandwidth_config *pll, u32 bw, u8 ratio);
	int (*set_gpio)(struct dvb_frontend *fe, u8 num, u8 dir, u8 val);
	void (*pwm_agc_reset)(struct dvb_frontend *fe);
	struct i2c_adapter *(*get_i2c_tuner)(struct dvb_frontend *fe);
	int (*tuner_sleep)(struct dvb_frontend *fe, int onoff);
	s32 (*get_adc_power)(struct dvb_frontend *fe, u8 mode);
	int (*get_dc_power)(struct dvb_frontend *fe, u8 IQ);
	u32 (*ctrl_timf)(struct dvb_frontend *fe, uint8_t op, uint32_t timf);
	enum frontend_tune_state (*get_tune_state)(struct dvb_frontend *fe);
	int (*set_tune_state)(struct dvb_frontend *fe, enum frontend_tune_state tune_state);
	int (*set_slave_frontend)(struct dvb_frontend *fe, struct dvb_frontend *fe_slave);
	int (*remove_slave_frontend)(struct dvb_frontend *fe);
	struct dvb_frontend *(*get_slave_frontend)(struct dvb_frontend *fe, int slave_index);
	int (*i2c_enumeration)(struct i2c_adapter *host, int no_of_demods,
		u8 default_addr, u8 first_addr, u8 is_dib8096p);
	struct i2c_adapter *(*get_i2c_master)(struct dvb_frontend *fe, enum dibx000_i2c_interface intf, int gating);
	int (*pid_filter_ctrl)(struct dvb_frontend *fe, u8 onoff);
	int (*pid_filter)(struct dvb_frontend *fe, u8 id, u16 pid, u8 onoff);
	struct dvb_frontend *(*init)(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib8000_config *cfg);
};

#if IS_REACHABLE(CONFIG_DVB_DIB8000)
void *dib8000_attach(struct dib8000_ops *ops);
#else
static inline int dib8000_attach(struct dib8000_ops *ops)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
