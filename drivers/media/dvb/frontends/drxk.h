#ifndef _DRXK_H_
#define _DRXK_H_

#include <linux/types.h>
#include <linux/i2c.h>

/**
 * struct drxk_config - Configure the initial parameters for DRX-K
 *
 * @adr:		I2C Address of the DRX-K
 * @parallel_ts:	True means that the device uses parallel TS,
 * 			Serial otherwise.
 * @dynamic_clk:	True means that the clock will be dynamically
 *			adjusted. Static clock otherwise.
 * @enable_merr_cfg:	Enable SIO_PDR_PERR_CFG/SIO_PDR_MVAL_CFG.
 * @single_master:	Device is on the single master mode
 * @no_i2c_bridge:	Don't switch the I2C bridge to talk with tuner
 * @antenna_gpio:	GPIO bit used to control the antenna
 * @antenna_dvbt:	GPIO bit for changing antenna to DVB-C. A value of 1
 *			means that 1=DVBC, 0 = DVBT. Zero means the opposite.
 * @mpeg_out_clk_strength: DRXK Mpeg output clock drive strength.
 * @microcode_name:	Name of the firmware file with the microcode
 *
 * On the *_gpio vars, bit 0 is UIO-1, bit 1 is UIO-2 and bit 2 is
 * UIO-3.
 */
struct drxk_config {
	u8	adr;
	bool	single_master;
	bool	no_i2c_bridge;
	bool	parallel_ts;
	bool	dynamic_clk;
	bool	enable_merr_cfg;

	bool	antenna_dvbt;
	u16	antenna_gpio;

	u8	mpeg_out_clk_strength;
	int	chunk_size;

	const char *microcode_name;
};

#if defined(CONFIG_DVB_DRXK) || (defined(CONFIG_DVB_DRXK_MODULE) \
        && defined(MODULE))
extern struct dvb_frontend *drxk_attach(const struct drxk_config *config,
					struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *drxk_attach(const struct drxk_config *config,
					struct i2c_adapter *i2c)
{
        printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
        return NULL;
}
#endif

#endif
