/* tuner-xc2028
 *
 * Copyright (c) 2007 Mauro Carvalho Chehab (mchehab@infradead.org)
 * This code is placed under the terms of the GNU General Public License v2
 */

/* xc2028 commands for callback */
#define XC2028_TUNER_RESET	0
#define XC2028_RESET_CLK	1

struct dvb_frontend;
struct i2c_client;

int xc2028_attach(struct i2c_client *c, struct dvb_frontend *fe);
