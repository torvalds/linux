#ifndef SI2168_PRIV_H
#define SI2168_PRIV_H

#include "si2168.h"
#include "dvb_frontend.h"
#include <linux/firmware.h>
#include <linux/i2c-mux.h>

#define SI2168_FIRMWARE "dvb-demod-si2168-01.fw"

/* state struct */
struct si2168 {
	struct i2c_client *client;
	struct i2c_adapter *adapter;
	struct mutex i2c_mutex;
	struct dvb_frontend fe;
	fe_delivery_system_t delivery_system;
	fe_status_t fe_status;
	bool active;
};

/* firmare command struct */
#define SI2157_ARGLEN      30
struct si2168_cmd {
	u8 args[SI2157_ARGLEN];
	unsigned wlen;
	unsigned rlen;
};

#endif
