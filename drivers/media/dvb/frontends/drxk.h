#ifndef _DRXK_H_
#define _DRXK_H_

#include <linux/types.h>
#include <linux/i2c.h>

struct drxk_config {
	u8 adr;
};

extern struct dvb_frontend *drxk_attach(const struct drxk_config *config,
					struct i2c_adapter *i2c,
					struct dvb_frontend **fe_t);
#endif
