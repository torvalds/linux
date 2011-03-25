#ifndef DIB9000_H
#define DIB9000_H

#include "dibx000_common.h"

struct dib9000_config {
	u8 dvbt_mode;
	u8 output_mpeg2_in_188_bytes;
	u8 hostbus_diversity;
	struct dibx000_bandwidth_config *bw;

	u16 if_drives;

	u32 timing_frequency;
	u32 xtal_clock_khz;
	u32 vcxo_timer;
	u32 demod_clock_khz;

	const u8 *microcode_B_fe_buffer;
	u32 microcode_B_fe_size;

	struct dibGPIOFunction gpio_function[2];
	struct dibSubbandSelection subband;

	u8 output_mode;
};

#define DEFAULT_DIB9000_I2C_ADDRESS 18

#if defined(CONFIG_DVB_DIB9000) || (defined(CONFIG_DVB_DIB9000_MODULE) && defined(MODULE))
extern struct dvb_frontend *dib9000_attach(struct i2c_adapter *i2c_adap, u8 i2c_addr, const struct dib9000_config *cfg);
extern int dib9000_i2c_enumeration(struct i2c_adapter *host, int no_of_demods, u8 default_addr, u8 first_addr);
extern struct i2c_adapter *dib9000_get_tuner_interface(struct dvb_frontend *fe);
extern struct i2c_adapter *dib9000_get_i2c_master(struct dvb_frontend *fe, enum dibx000_i2c_interface intf, int gating);
extern int dib9000_set_gpio(struct dvb_frontend *fe, u8 num, u8 dir, u8 val);
extern int dib9000_fw_pid_filter_ctrl(struct dvb_frontend *fe, u8 onoff);
extern int dib9000_fw_pid_filter(struct dvb_frontend *fe, u8 id, u16 pid, u8 onoff);
extern int dib9000_firmware_post_pll_init(struct dvb_frontend *fe);
extern int dib9000_set_slave_frontend(struct dvb_frontend *fe, struct dvb_frontend *fe_slave);
extern int dib9000_remove_slave_frontend(struct dvb_frontend *fe);
extern struct dvb_frontend *dib9000_get_slave_frontend(struct dvb_frontend *fe, int slave_index);
extern struct i2c_adapter *dib9000_get_component_bus_interface(struct dvb_frontend *fe);
extern int dib9000_set_i2c_adapter(struct dvb_frontend *fe, struct i2c_adapter *i2c);
extern int dib9000_fw_set_component_bus_speed(struct dvb_frontend *fe, u16 speed);
#else
static inline struct dvb_frontend *dib9000_attach(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib9000_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline struct i2c_adapter *dib9000_get_i2c_master(struct dvb_frontend *fe, enum dibx000_i2c_interface intf, int gating)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline int dib9000_i2c_enumeration(struct i2c_adapter *host, int no_of_demods, u8 default_addr, u8 first_addr)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline struct i2c_adapter *dib9000_get_tuner_interface(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline int dib9000_set_gpio(struct dvb_frontend *fe, u8 num, u8 dir, u8 val)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib9000_fw_pid_filter_ctrl(struct dvb_frontend *fe, u8 onoff)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib9000_fw_pid_filter(struct dvb_frontend *fe, u8 id, u16 pid, u8 onoff)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib9000_firmware_post_pll_init(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib9000_set_slave_frontend(struct dvb_frontend *fe, struct dvb_frontend *fe_slave)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

int dib9000_remove_slave_frontend(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline struct dvb_frontend *dib9000_get_slave_frontend(struct dvb_frontend *fe, int slave_index)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline struct i2c_adapter *dib9000_get_component_bus_interface(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline int dib9000_set_i2c_adapter(struct dvb_frontend *fe, struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int dib9000_fw_set_component_bus_speed(struct dvb_frontend *fe, u16 speed)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}
#endif

#endif
