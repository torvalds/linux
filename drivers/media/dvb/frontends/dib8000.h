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

int dib8000_i2c_enumeration(struct i2c_adapter *host, int no_of_demods, u8 default_addr, u8 first_addr)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

int dib8000_set_gpio(struct dvb_frontend *fe, u8 num, u8 dir, u8 val)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

int dib8000_set_wbd_ref(struct dvb_frontend *fe, u16 value)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}
#endif

#endif
