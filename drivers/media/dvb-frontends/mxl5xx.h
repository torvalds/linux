#ifndef _MXL5XX_H_
#define _MXL5XX_H_

#include <linux/types.h>
#include <linux/i2c.h>

#include <media/dvb_frontend.h>

struct mxl5xx_cfg {
	u8   adr;
	u8   type;
	u32  cap;
	u32  clk;
	u32  ts_clk;

	u8  *fw;
	u32  fw_len;

	int (*fw_read)(void *priv, u8 *buf, u32 len);
	void *fw_priv;
};

#if IS_REACHABLE(CONFIG_DVB_MXL5XX)

extern struct dvb_frontend *mxl5xx_attach(struct i2c_adapter *i2c,
	struct mxl5xx_cfg *cfg, u32 demod, u32 tuner,
	int (**fn_set_input)(struct dvb_frontend *, int));

#else

static inline struct dvb_frontend *mxl5xx_attach(struct i2c_adapter *i2c,
	struct mxl5xx_cfg *cfg, u32 demod, u32 tuner,
	int (**fn_set_input)(struct dvb_frontend *, int))
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif /* CONFIG_DVB_MXL5XX */

#endif /* _MXL5XX_H_ */
