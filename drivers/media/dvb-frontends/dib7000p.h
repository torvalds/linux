#ifndef DIB7000P_H
#define DIB7000P_H

#include <linux/kconfig.h>

#include "dibx000_common.h"

struct dib7000p_config {
	u8 output_mpeg2_in_188_bytes;
	u8 hostbus_diversity;
	u8 tuner_is_baseband;
	int (*update_lna) (struct dvb_frontend *, u16 agc_global);

	u8 agc_config_count;
	struct dibx000_agc_config *agc;
	struct dibx000_bandwidth_config *bw;

#define DIB7000P_GPIO_DEFAULT_DIRECTIONS 0xffff
	u16 gpio_dir;
#define DIB7000P_GPIO_DEFAULT_VALUES     0x0000
	u16 gpio_val;
#define DIB7000P_GPIO_PWM_POS0(v)        ((v & 0xf) << 12)
#define DIB7000P_GPIO_PWM_POS1(v)        ((v & 0xf) << 8 )
#define DIB7000P_GPIO_PWM_POS2(v)        ((v & 0xf) << 4 )
#define DIB7000P_GPIO_PWM_POS3(v)         (v & 0xf)
#define DIB7000P_GPIO_DEFAULT_PWM_POS    0xffff
	u16 gpio_pwm_pos;

	u16 pwm_freq_div;

	u8 quartz_direct;

	u8 spur_protect;

	int (*agc_control) (struct dvb_frontend *, u8 before);

	u8 output_mode;
	u8 disable_sample_and_hold:1;

	u8 enable_current_mirror:1;
	u16 diversity_delay;

	u8 default_i2c_addr;
	u8 enMpegOutput:1;
};

#define DEFAULT_DIB7000P_I2C_ADDRESS 18

struct dib7000p_ops {
	int (*set_wbd_ref)(struct dvb_frontend *demod, u16 value);
	int (*get_agc_values)(struct dvb_frontend *fe,
		u16 *agc_global, u16 *agc1, u16 *agc2, u16 *wbd);
	int (*set_agc1_min)(struct dvb_frontend *fe, u16 v);
	int (*update_pll)(struct dvb_frontend *fe, struct dibx000_bandwidth_config *bw);
	int (*set_gpio)(struct dvb_frontend *demod, u8 num, u8 dir, u8 val);
	u32 (*ctrl_timf)(struct dvb_frontend *fe, u8 op, u32 timf);
	int (*dib7000pc_detection)(struct i2c_adapter *i2c_adap);
	struct i2c_adapter *(*get_i2c_master)(struct dvb_frontend *demod, enum dibx000_i2c_interface intf, int gating);
	int (*pid_filter_ctrl)(struct dvb_frontend *fe, u8 onoff);
	int (*pid_filter)(struct dvb_frontend *fe, u8 id, u16 pid, u8 onoff);
	int (*i2c_enumeration)(struct i2c_adapter *i2c, int no_of_demods, u8 default_addr, struct dib7000p_config cfg[]);
	struct i2c_adapter *(*get_i2c_tuner)(struct dvb_frontend *fe);
	int (*tuner_sleep)(struct dvb_frontend *fe, int onoff);
	int (*get_adc_power)(struct dvb_frontend *fe);
	int (*slave_reset)(struct dvb_frontend *fe);
	struct dvb_frontend *(*init)(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib7000p_config *cfg);
};

#if IS_ENABLED(CONFIG_DVB_DIB7000P)
void *dib7000p_attach(struct dib7000p_ops *ops);
#else
static inline void *dib7000p_attach(struct dib7000p_ops *ops)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
