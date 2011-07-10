#ifndef _DRXK_H_
#define _DRXK_H_

#include <linux/types.h>
#include <linux/i2c.h>

struct drxk_config {
	u8 adr;
	u32 single_master : 1;
	u32 no_i2c_bridge : 1;
	const char *microcode_name;
};

extern struct dvb_frontend *drxk_attach(const struct drxk_config *config,
					struct i2c_adapter *i2c,
					struct dvb_frontend **fe_t);
#endif
