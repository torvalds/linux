/*
   Driver for the Nxt2002 demodulator
*/

#ifndef NXT2002_H
#define NXT2002_H

#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

struct nxt2002_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* request firmware for device */
	int (*request_firmware)(struct dvb_frontend* fe, const struct firmware **fw, char* name);
};

extern struct dvb_frontend* nxt2002_attach(const struct nxt2002_config* config,
					   struct i2c_adapter* i2c);

#endif // NXT2002_H
