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
};

#define DEFAULT_DIB8000_I2C_ADDRESS 18

#if defined(CONFIG_DVB_DIB8000) || (defined(CONFIG_DVB_DIB8000_MODULE) && defined(MODULE))
extern struct dvb_frontend *dib8000_attach(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib8000_config *cfg);
extern struct i2c_adapter *dib8000_get_i2c_master(struct dvb_frontend *, enum dibx000_i2c_interface, int);

extern int dib8000_i2c_enumeration(struct i2c_adapter *host, int no_of_demods, u8 default_addr, u8 first_addr);

extern int dib8000_set_gpio(struct dvb_frontend *, u8 num, u8 dir, u8 val);
extern int dib8000_set_wbd_ref(struct dvb_frontend *, u16 value);
extern int dib8000_pid_filter_ctrl(struct dvb_frontend *, u8 onoff);
extern int dib8000_pid_filter(struct dvb_frontend *, u8 id, u16 pid, u8 onoff);
extern int dib8000_set_tune_state(struct dvb_frontend *fe, enum frontend_tune_state tune_state);
extern enum frontend_tune_state dib8000_get_tune_state(struct dvb_frontend *fe);
extern void dib8000_pwm_agc_reset(struct dvb_frontend *fe);
extern s32 dib8000_get_adc_power(struct dvb_frontend *fe, u8 mode);
extern int dib8000_set_slave_frontend(struct dvb_frontend *fe, struct dvb_frontend *fe_slave);
extern int dib8000_remove_slave_frontend(struct dvb_frontend *fe);
extern struct dvb_frontend *dib8000_get_slave_frontend(struct dvb_frontend *fe, int slave_index);
#else
static inline struct dvb_frontend *dib8000_attach(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib8000_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline struct i2c_adapter *dib8000_get_i2c_master(struct dvb_frontend *fe, enum dibx000_i2c_interface i, int x)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline int dib8000_i2c_enumeration(struct i2c_adapter *host, int no_of_demods, u8 default_addr, u8 first_addr)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib8000_set_gpio(struct dvb_frontend *fe, u8 num, u8 dir, u8 val)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib8000_set_wbd_ref(struct dvb_frontend *fe, u16 value)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib8000_pid_filter_ctrl(struct dvb_frontend *fe, u8 onoff)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib8000_pid_filter(struct dvb_frontend *fe, u8 id, u16 pid, u8 onoff)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}
static inline int dib8000_set_tune_state(struct dvb_frontend *fe, enum frontend_tune_state tune_state)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}
static inline enum frontend_tune_state dib8000_get_tune_state(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return CT_SHUTDOWN;
}
static inline void dib8000_pwm_agc_reset(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
}
static inline s32 dib8000_get_adc_power(struct dvb_frontend *fe, u8 mode)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return 0;
}
static inline int dib8000_set_slave_frontend(struct dvb_frontend *fe, struct dvb_frontend *fe_slave)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

int dib8000_remove_slave_frontend(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline struct dvb_frontend *dib8000_get_slave_frontend(struct dvb_frontend *fe, int slave_index)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
