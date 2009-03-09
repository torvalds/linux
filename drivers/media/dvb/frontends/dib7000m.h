#ifndef DIB7000M_H
#define DIB7000M_H

#include "dibx000_common.h"

struct dib7000m_config {
	u8 dvbt_mode;
	u8 output_mpeg2_in_188_bytes;
	u8 hostbus_diversity;
	u8 tuner_is_baseband;
	u8 mobile_mode;
	int (*update_lna) (struct dvb_frontend *, u16 agc_global);

	u8 agc_config_count;
	struct dibx000_agc_config *agc;

	struct dibx000_bandwidth_config *bw;

#define DIB7000M_GPIO_DEFAULT_DIRECTIONS 0xffff
	u16 gpio_dir;
#define DIB7000M_GPIO_DEFAULT_VALUES     0x0000
	u16 gpio_val;
#define DIB7000M_GPIO_PWM_POS0(v)        ((v & 0xf) << 12)
#define DIB7000M_GPIO_PWM_POS1(v)        ((v & 0xf) << 8 )
#define DIB7000M_GPIO_PWM_POS2(v)        ((v & 0xf) << 4 )
#define DIB7000M_GPIO_PWM_POS3(v)         (v & 0xf)
#define DIB7000M_GPIO_DEFAULT_PWM_POS    0xffff
	u16 gpio_pwm_pos;

	u16 pwm_freq_div;

	u8 quartz_direct;

	u8 input_clk_is_div_2;

	int (*agc_control) (struct dvb_frontend *, u8 before);
};

#define DEFAULT_DIB7000M_I2C_ADDRESS 18

#if defined(CONFIG_DVB_DIB7000M) || (defined(CONFIG_DVB_DIB7000M_MODULE) && \
				     defined(MODULE))
extern struct dvb_frontend *dib7000m_attach(struct i2c_adapter *i2c_adap,
					    u8 i2c_addr,
					    struct dib7000m_config *cfg);
extern struct i2c_adapter *dib7000m_get_i2c_master(struct dvb_frontend *,
						   enum dibx000_i2c_interface,
						   int);
#else
static inline
struct dvb_frontend *dib7000m_attach(struct i2c_adapter *i2c_adap,
				     u8 i2c_addr, struct dib7000m_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline
struct i2c_adapter *dib7000m_get_i2c_master(struct dvb_frontend *demod,
					    enum dibx000_i2c_interface intf,
					    int gating)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

/* TODO
extern INT dib7000m_set_gpio(struct dibDemod *demod, UCHAR num, UCHAR dir, UCHAR val);
extern INT dib7000m_enable_vbg_voltage(struct dibDemod *demod);
extern void dib7000m_set_hostbus_diversity(struct dibDemod *demod, UCHAR onoff);
extern USHORT dib7000m_get_current_agc_global(struct dibDemod *demod);
*/

#endif
