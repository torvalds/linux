/*
   Driver for the Spase sp887x demodulator
*/

#ifndef SP887X_H
#define SP887X_H

#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

struct sp887x_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* request firmware for device */
	int (*request_firmware)(struct dvb_frontend* fe, const struct firmware **fw, char* name);
};

#if IS_REACHABLE(CONFIG_DVB_SP887X)
extern struct dvb_frontend* sp887x_attach(const struct sp887x_config* config,
					  struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* sp887x_attach(const struct sp887x_config* config,
					  struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_SP887X

#endif // SP887X_H
