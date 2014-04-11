#ifndef SI2157_PRIV_H
#define SI2157_PRIV_H

#include "si2157.h"

/* state struct */
struct si2157 {
	struct mutex i2c_mutex;
	struct i2c_client *client;
	struct dvb_frontend *fe;
	bool active;
};

/* firmare command struct */
#define SI2157_ARGLEN      30
struct si2157_cmd {
	u8 args[SI2157_ARGLEN];
	unsigned len;
};

#endif
