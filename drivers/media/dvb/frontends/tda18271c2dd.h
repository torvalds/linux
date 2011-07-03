#ifndef _TDA18271C2DD_H_
#define _TDA18271C2DD_H_
struct dvb_frontend *tda18271c2dd_attach(struct dvb_frontend *fe,
					 struct i2c_adapter *i2c, u8 adr);
#endif
