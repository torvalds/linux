#ifndef _DRXK_H_
#define _DRXK_H_

#include <linux/types.h>
#include <linux/i2c.h>

/**
 * struct drxk_config - Configure the initial parameters for DRX-K
 *
 * adr:			I2C Address of the DRX-K
 * single_master:	Device is on the single master mode
 * no_i2c_bridge:	Don't switch the I2C bridge to talk with tuner
 * antenna_uses_gpio:	Use GPIO to control the antenna
 * antenna_dvbc:	GPIO for changing antenna to DVB-C
 * antenna_dvbt:	GPIO for changing antenna to DVB-T
 * microcode_name:	Name of the firmware file with the microcode
 */
struct drxk_config {
	u8	adr;
	bool	single_master;
	bool	no_i2c_bridge;

	bool	antenna_uses_gpio;
	u16	antenna_dvbc, antenna_dvbt;

	const char *microcode_name;
};

extern struct dvb_frontend *drxk_attach(const struct drxk_config *config,
					struct i2c_adapter *i2c,
					struct dvb_frontend **fe_t);
#endif
