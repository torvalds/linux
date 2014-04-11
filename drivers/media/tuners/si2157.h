#ifndef SI2157_H
#define SI2157_H

#include <linux/kconfig.h>
#include "dvb_frontend.h"

/*
 * I2C address
 * 0x60
 */
struct si2157_config {
	/*
	 * frontend
	 */
	struct dvb_frontend *fe;
};

#endif
