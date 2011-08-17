#ifndef DIB7000P_H
#define DIB7000P_H

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

#if defined(CONFIG_DVB_DIB7000P) || (defined(CONFIG_DVB_DIB7000P_MODULE) && \
					defined(MODULE))
extern struct dvb_frontend *dib7000p_attach(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib7000p_config *cfg);
extern struct i2c_adapter *dib7000p_get_i2c_master(struct dvb_frontend *, enum dibx000_i2c_interface, int);
extern int dib7000p_i2c_enumeration(struct i2c_adapter *i2c, int no_of_demods, u8 default_addr, struct dib7000p_config cfg[]);
extern int dib7000p_set_gpio(struct dvb_frontend *, u8 num, u8 dir, u8 val);
extern int dib7000p_set_wbd_ref(struct dvb_frontend *, u16 value);
extern int dib7000pc_detection(struct i2c_adapter *i2c_adap);
extern int dib7000p_pid_filter(struct dvb_frontend *, u8 id, u16 pid, u8 onoff);
extern int dib7000p_pid_filter_ctrl(struct dvb_frontend *fe, u8 onoff);
extern int dib7000p_update_pll(struct dvb_frontend *fe, struct dibx000_bandwidth_config *bw);
extern u32 dib7000p_ctrl_timf(struct dvb_frontend *fe, u8 op, u32 timf);
extern int dib7090_agc_restart(struct dvb_frontend *fe, u8 restart);
extern int dib7090_tuner_sleep(struct dvb_frontend *fe, int onoff);
extern int dib7090_get_adc_power(struct dvb_frontend *fe);
extern struct i2c_adapter *dib7090_get_i2c_tuner(struct dvb_frontend *fe);
extern int dib7090_slave_reset(struct dvb_frontend *fe);
#else
static inline struct dvb_frontend *dib7000p_attach(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib7000p_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline struct i2c_adapter *dib7000p_get_i2c_master(struct dvb_frontend *fe, enum dibx000_i2c_interface i, int x)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline int dib7000p_i2c_enumeration(struct i2c_adapter *i2c, int no_of_demods, u8 default_addr, struct dib7000p_config cfg[])
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib7000p_set_gpio(struct dvb_frontend *fe, u8 num, u8 dir, u8 val)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib7000p_set_wbd_ref(struct dvb_frontend *fe, u16 value)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib7000pc_detection(struct i2c_adapter *i2c_adap)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib7000p_pid_filter(struct dvb_frontend *fe, u8 id, u16 pid, u8 onoff)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib7000p_pid_filter_ctrl(struct dvb_frontend *fe, uint8_t onoff)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib7000p_update_pll(struct dvb_frontend *fe, struct dibx000_bandwidth_config *bw)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline u32 dib7000p_ctrl_timf(struct dvb_frontend *fe, u8 op, u32 timf)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return 0;
}

static inline int dib7090_agc_restart(struct dvb_frontend *fe, u8 restart)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib7090_tuner_sleep(struct dvb_frontend *fe, int onoff)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib7090_get_adc_power(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline struct i2c_adapter *dib7090_get_i2c_tuner(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline int dib7090_slave_reset(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}
#endif

#endif
