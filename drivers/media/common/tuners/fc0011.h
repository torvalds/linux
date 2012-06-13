#ifndef LINUX_FC0011_H_
#define LINUX_FC0011_H_

#include "dvb_frontend.h"


/** struct fc0011_config - fc0011 hardware config
 *
 * @i2c_address: I2C bus address.
 */
struct fc0011_config {
	u8 i2c_address;
};

/** enum fc0011_fe_callback_commands - Frontend callbacks
 *
 * @FC0011_FE_CALLBACK_POWER: Power on tuner hardware.
 * @FC0011_FE_CALLBACK_RESET: Request a tuner reset.
 */
enum fc0011_fe_callback_commands {
	FC0011_FE_CALLBACK_POWER,
	FC0011_FE_CALLBACK_RESET,
};

#if defined(CONFIG_MEDIA_TUNER_FC0011) ||\
    defined(CONFIG_MEDIA_TUNER_FC0011_MODULE)
struct dvb_frontend *fc0011_attach(struct dvb_frontend *fe,
				   struct i2c_adapter *i2c,
				   const struct fc0011_config *config);
#else
static inline
struct dvb_frontend *fc0011_attach(struct dvb_frontend *fe,
				   struct i2c_adapter *i2c,
				   const struct fc0011_config *config)
{
	dev_err(&i2c->dev, "fc0011 driver disabled in Kconfig\n");
	return NULL;
}
#endif

#endif /* LINUX_FC0011_H_ */
