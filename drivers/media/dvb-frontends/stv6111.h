#ifndef _STV6111_H_
#define _STV6111_H_

#if IS_REACHABLE(CONFIG_DVB_STV6111)

extern struct dvb_frontend *stv6111_attach(struct dvb_frontend *fe,
				struct i2c_adapter *i2c, u8 adr);

#else

static inline struct dvb_frontend *stv6111_attach(struct dvb_frontend *fe,
				struct i2c_adapter *i2c, u8 adr)
{
	pr_warn("%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif /* CONFIG_DVB_STV6111 */

#endif /* _STV6111_H_ */
